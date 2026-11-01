#include <nix/daemon.hxx>
#include <nix/logging.hxx>
#include <nix/shared_memory_header.hxx>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

struct Registration {
    std::string nodeName;
    std::string topicName;
    std::string typeName;
    nix::EndpointRole role;
    std::uint64_t capacity;
};

struct TopicSample {
    std::string nodeName;
    std::string topicName;
    std::string typeName;
    std::uint64_t publishedAtMillis;
    std::uint64_t originalSize;
    std::string payload;
};

std::mutex registryMutex;
std::vector<Registration> registry;
std::vector<TopicSample> topicSamples;
std::string sniffTopic;
std::string sniffOwnerNode;
constexpr std::size_t maxSniffPayloadBytes = 256U;
constexpr std::size_t maxTopicSamples = 32U;
std::atomic<bool> running{true};
int listeningSocket = -1;

void logEvent(const std::string& message) {
    NIX_INFO("{}", message);
}

void handleSignal(int) {
    running.store(false);
    if (listeningSocket != -1) {
        close(listeningSocket);
        listeningSocket = -1;
    }
}

std::string encode(std::string_view value) {
    constexpr char hexDigits[] = "0123456789abcdef";
    std::string result;
    result.reserve(value.size() * std::size_t{2U});
    for (const char rawCharacter : value) {
        const auto character = static_cast<unsigned char>(rawCharacter);
        result.push_back(hexDigits[character >> 4U]);
        result.push_back(hexDigits[character & 0x0fU]);
    }
    return result;
}

int decodeHexDigit(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::string decode(std::string_view value) {
    if ((value.size() % 2U) != 0U) {
        throw std::runtime_error("invalid hex field");
    }
    std::string result;
    result.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        const int high = decodeHexDigit(value[index]);
        const int low = decodeHexDigit(value[index + 1U]);
        if (high < 0 || low < 0) {
            throw std::runtime_error("invalid hex field");
        }
        result.push_back(static_cast<char>((static_cast<unsigned int>(high) << 4U)
            | static_cast<unsigned int>(low)));
    }
    return result;
}

std::vector<std::string_view> split(std::string_view line) {
    std::vector<std::string_view> fields;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t separator = line.find('\t', begin);
        if (separator == std::string_view::npos) {
            fields.push_back(line.substr(begin));
            return fields;
        }
        fields.push_back(line.substr(begin, separator - begin));
        begin = separator + 1U;
    }
}

std::string readLine(int socket) {
    std::string line;
    while (line.size() < std::size_t{1024U} * 1024U) {
        char character = 0;
        const ssize_t result = recv(socket, &character, sizeof(character), 0);
        if (result < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("socket read failed");
        }
        if (result == 0) return {};
        if (character == '\n') return line;
        line.push_back(character);
    }
    throw std::runtime_error("request exceeded maximum line length");
}

bool sendLine(int socket, std::string_view line) {
    std::string message(line);
    message.push_back('\n');
    std::size_t sent = 0;
    while (sent < message.size()) {
        const ssize_t result = send(socket, message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
        if (result < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (result == 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

bool parseCapacity(std::string_view value, std::uint64_t& capacity) {
    if (value.empty()) return false;
    capacity = 0;
    for (const char digit : value) {
        if (digit < '0' || digit > '9') return false;
        const std::uint64_t numericDigit = static_cast<std::uint64_t>(digit - '0');
        if (capacity > (UINT64_MAX - numericDigit) / 10U) return false;
        capacity = capacity * 10U + numericDigit;
    }
    return true;
}

bool setSharedMemorySniffFlag(std::string_view topicName, bool enabled) {
    if (topicName.empty()) return false;
    const std::string shmPath = topicName.front() == '/' ? std::string(topicName) : "/" + std::string(topicName);
    const int descriptor = shm_open(shmPath.c_str(), O_RDWR, 0);
    if (descriptor == -1) return false;

    struct stat status{};
    if (fstat(descriptor, &status) == -1 || status.st_size < static_cast<off_t>(sizeof(nix::SharedMemoryHeader))) {
        close(descriptor);
        return false;
    }
    void* mapped = mmap(nullptr, sizeof(nix::SharedMemoryHeader), PROT_READ | PROT_WRITE, MAP_SHARED, descriptor, 0);
    close(descriptor);
    if (mapped == MAP_FAILED) return false;

    auto& header = *static_cast<nix::SharedMemoryHeader*>(mapped);
    const bool valid = header.magic == nix::sharedMemoryMagic
        && header.version == nix::sharedMemoryVersion
        && header.headerSize >= sizeof(nix::SharedMemoryHeader)
        && header.initializationState.load(std::memory_order_acquire) == nix::sharedMemoryReady;
    if (valid) header.sniffEnabled.store(enabled ? 1U : 0U, std::memory_order_release);
    munmap(mapped, sizeof(nix::SharedMemoryHeader));
    return valid;
}

void handleClient(int clientSocket) {
    std::string nodeName;
    std::vector<Registration> sessionRegistrations;
    try {
        const std::string helloLine = readLine(clientSocket);
        const auto hello = split(helloLine);
        if (hello.size() != 2U || hello[0] != "HELLO") {
            sendLine(clientSocket, "ERROR\tinvalid handshake");
            close(clientSocket);
            return;
        }
        nodeName = decode(hello[1]);
        if (nodeName.empty()) {
            sendLine(clientSocket, "ERROR\tempty node name");
            close(clientSocket);
            return;
        }
        if (!sendLine(clientSocket, "OK")) {
            close(clientSocket);
            return;
        }
        logEvent("node connected: " + nodeName);

        for (;;) {
            const std::string request = readLine(clientSocket);
            if (request.empty()) break;
            const auto fields = split(request);
            if (fields.size() == 5U && fields[0] == "REGISTER") {
                std::uint64_t capacity = 0;
                const bool isPublisher = fields[1] == "P";
                const bool isSubscriber = fields[1] == "S";
                if ((!isPublisher && !isSubscriber) || !parseCapacity(fields[2], capacity)) {
                    sendLine(clientSocket, "ERROR\tinvalid registration");
                    continue;
                }
                Registration registration{nodeName, decode(fields[3]), decode(fields[4]),
                    isPublisher ? nix::EndpointRole::Publisher : nix::EndpointRole::Subscriber, capacity};
                {
                    std::lock_guard<std::mutex> lock(registryMutex);
                    registry.push_back(registration);
                    if (registration.role == nix::EndpointRole::Publisher && registration.topicName == sniffTopic) {
                        setSharedMemorySniffFlag(sniffTopic, true);
                    }
                }
                sessionRegistrations.push_back(std::move(registration));
                const Registration& registered = sessionRegistrations.back();
                const std::string roleName = registered.role == nix::EndpointRole::Publisher ? "publisher" : "subscriber";
                logEvent("registered " + roleName + " node=" + registered.nodeName
                    + " topic=" + registered.topicName + " type=" + registered.typeName
                    + " capacity=" + std::to_string(registered.capacity));
                sendLine(clientSocket, "OK");
            } else if (fields.size() == 5U && fields[0] == "UNREGISTER") {
                std::uint64_t capacity = 0;
                const bool isPublisher = fields[1] == "P";
                const bool isSubscriber = fields[1] == "S";
                if ((!isPublisher && !isSubscriber) || !parseCapacity(fields[2], capacity)) {
                    sendLine(clientSocket, "ERROR\tinvalid unregistration");
                    continue;
                }
                const Registration endpoint{nodeName, decode(fields[3]), decode(fields[4]),
                    isPublisher ? nix::EndpointRole::Publisher : nix::EndpointRole::Subscriber, capacity};
                const auto sessionEntry = std::find_if(sessionRegistrations.begin(), sessionRegistrations.end(),
                    [&](const Registration& current) {
                        return current.topicName == endpoint.topicName
                            && current.typeName == endpoint.typeName
                            && current.role == endpoint.role
                            && current.capacity == endpoint.capacity;
                    });
                if (sessionEntry == sessionRegistrations.end()) {
                    sendLine(clientSocket, "ERROR\tendpoint is not registered");
                    continue;
                }
                {
                    std::lock_guard<std::mutex> lock(registryMutex);
                    const auto registryEntry = std::find_if(registry.begin(), registry.end(),
                        [&](const Registration& current) {
                            return current.nodeName == endpoint.nodeName
                                && current.topicName == endpoint.topicName
                                && current.typeName == endpoint.typeName
                                && current.role == endpoint.role
                                && current.capacity == endpoint.capacity;
                        });
                    if (registryEntry != registry.end()) {
                        registry.erase(registryEntry);
                    }
                    if (endpoint.role == nix::EndpointRole::Publisher && endpoint.topicName == sniffTopic) {
                        setSharedMemorySniffFlag(sniffTopic, false);
                        sniffTopic.clear();
                        sniffOwnerNode.clear();
                    }
                }
                sessionRegistrations.erase(sessionEntry);
                const std::string roleName = endpoint.role == nix::EndpointRole::Publisher ? "publisher" : "subscriber";
                logEvent("unregistered " + roleName + " node=" + nodeName + " topic=" + endpoint.topicName);
                sendLine(clientSocket, "OK");
            } else if (fields.size() == 2U && fields[0] == "DISCOVER") {
                const std::string topicFilter = fields[1] == "-" ? std::string{} : decode(fields[1]);
                std::vector<Registration> matches;
                {
                    std::lock_guard<std::mutex> lock(registryMutex);
                    for (const auto& registration : registry) {
                        if (topicFilter.empty() || registration.topicName == topicFilter) {
                            matches.push_back(registration);
                        }
                    }
                }
                logEvent("discovery node=" + nodeName + " topic="
                    + (topicFilter.empty() ? std::string("*") : topicFilter)
                    + " results=" + std::to_string(matches.size()));
                for (const auto& registration : matches) {
                    const char* role = registration.role == nix::EndpointRole::Publisher ? "P" : "S";
                    const std::string response = "ENTRY\t" + std::string(role) + "\t" + encode(registration.nodeName)
                        + "\t" + encode(registration.topicName) + "\t" + encode(registration.typeName)
                        + "\t" + std::to_string(registration.capacity);
                    if (!sendLine(clientSocket, response)) break;
                }
                if (!sendLine(clientSocket, "END")) break;
            } else if (fields.size() == 5U && fields[0] == "SAMPLE") {
                std::uint64_t originalSize = 0;
                if (!parseCapacity(fields[3], originalSize)) {
                    sendLine(clientSocket, "ERROR\tinvalid sample size");
                    continue;
                }
                const std::string topicName = decode(fields[1]);
                const std::string typeName = decode(fields[2]);
                const std::string payload = decode(fields[4]);
                if (topicName.empty() || typeName.empty() || payload.size() > maxSniffPayloadBytes
                    || originalSize < payload.size()) {
                    sendLine(clientSocket, "ERROR\tinvalid sample payload");
                    continue;
                }
                const auto now = std::chrono::system_clock::now().time_since_epoch();
                const auto publishedAtMillis = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
                bool registeredPublisher = false;
                {
                    std::lock_guard<std::mutex> lock(registryMutex);
                    if (topicName != sniffTopic) {
                        if (!sendLine(clientSocket, "OK")) break;
                        continue;
                    }
                    registeredPublisher = std::any_of(registry.begin(), registry.end(), [&](const Registration& current) {
                        return current.nodeName == nodeName && current.topicName == topicName
                            && current.typeName == typeName && current.role == nix::EndpointRole::Publisher;
                    });
                    if (registeredPublisher) {
                        const auto existing = std::find_if(topicSamples.begin(), topicSamples.end(),
                            [&](const TopicSample& sample) {
                                return sample.nodeName == nodeName && sample.topicName == topicName
                                    && sample.typeName == typeName;
                            });
                        if (existing != topicSamples.end()) {
                            *existing = TopicSample{nodeName, topicName, typeName, publishedAtMillis,
                                originalSize, payload};
                        } else {
                            if (topicSamples.size() == maxTopicSamples) {
                                topicSamples.erase(topicSamples.begin());
                            }
                            topicSamples.push_back(TopicSample{nodeName, topicName, typeName,
                                publishedAtMillis, originalSize, payload});
                        }
                    }
                }
                if (!registeredPublisher) {
                    sendLine(clientSocket, "ERROR\tpublisher endpoint is not registered");
                    continue;
                }
                if (!sendLine(clientSocket, "OK")) break;
            } else if (fields.size() == 2U && fields[0] == "SNIFF") {
                const std::string topicFilter = fields[1] == "-" ? std::string{} : decode(fields[1]);
                std::vector<TopicSample> matches;
                {
                    std::lock_guard<std::mutex> lock(registryMutex);
                    for (const TopicSample& sample : topicSamples) {
                        if (topicFilter.empty() || sample.topicName == topicFilter) {
                            matches.push_back(sample);
                        }
                    }
                }
                for (const TopicSample& sample : matches) {
                    const std::string response = "SAMPLE_ENTRY\t" + encode(sample.nodeName)
                        + "\t" + encode(sample.topicName) + "\t" + encode(sample.typeName)
                        + "\t" + std::to_string(sample.originalSize)
                        + "\t" + std::to_string(sample.publishedAtMillis)
                        + "\t" + encode(sample.payload);
                    if (!sendLine(clientSocket, response)) break;
                }
                if (!sendLine(clientSocket, "END")) break;
            } else if (fields.size() == 2U && fields[0] == "SNIFF_SET") {
                const std::string requestedTopic = fields[1] == "-" ? std::string{} : decode(fields[1]);
                std::lock_guard<std::mutex> lock(registryMutex);
                if (!requestedTopic.empty()) {
                    const bool publisherExists = std::any_of(registry.begin(), registry.end(),
                        [&](const Registration& current) {
                            return current.topicName == requestedTopic && current.role == nix::EndpointRole::Publisher;
                        });
                    if (!publisherExists) {
                        sendLine(clientSocket, "ERROR\tno publisher is registered for the selected topic");
                        continue;
                    }
                    if (!setSharedMemorySniffFlag(requestedTopic, true)) {
                        sendLine(clientSocket, "ERROR\tunable to arm shared-memory topic");
                        continue;
                    }
                    topicSamples.erase(std::remove_if(topicSamples.begin(), topicSamples.end(),
                        [&](const TopicSample& sample) { return sample.topicName == requestedTopic; }),
                        topicSamples.end());
                }
                if (!sniffTopic.empty() && sniffTopic != requestedTopic) {
                    setSharedMemorySniffFlag(sniffTopic, false);
                }
                sniffTopic = requestedTopic;
                sniffOwnerNode = requestedTopic.empty() ? std::string{} : nodeName;
                if (!sendLine(clientSocket, "OK")) break;
            } else {
                if (!sendLine(clientSocket, "ERROR\tunknown request")) break;
            }
        }
    } catch (const std::exception& error) {
        NIX_ERROR("nix-daemon client error: {}", error.what());
    }

    {
        std::lock_guard<std::mutex> lock(registryMutex);
        if (sniffOwnerNode == nodeName && !sniffTopic.empty()) {
            setSharedMemorySniffFlag(sniffTopic, false);
            sniffTopic.clear();
            sniffOwnerNode.clear();
        }
        for (const auto& registration : sessionRegistrations) {
            const auto found = std::find_if(registry.begin(), registry.end(), [&](const Registration& current) {
                return current.nodeName == registration.nodeName
                    && current.topicName == registration.topicName
                    && current.typeName == registration.typeName
                    && current.role == registration.role
                    && current.capacity == registration.capacity;
            });
            if (found != registry.end()) registry.erase(found);
        }
    }
    if (!nodeName.empty()) {
        logEvent("node disconnected: " + nodeName + " endpoints=" + std::to_string(sessionRegistrations.size()));
    }
    close(clientSocket);
}

} // namespace

int main(int argc, char** argv) {
    std::string socketPath = nix::defaultDaemonSocketPath();
    if (argc == 3 && std::string_view(argv[1]) == "--socket") {
        socketPath = argv[2];
    } else if (argc != 1) {
        NIX_ERROR("Usage: nix-daemon [--socket PATH]");
        return 2;
    }
    if (socketPath.size() >= sizeof(sockaddr_un::sun_path)) {
        NIX_ERROR("nix-daemon socket path is too long");
        return 2;
    }

    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    listeningSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listeningSocket == -1) {
        NIX_ERROR("socket creation failed");
        return 1;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::copy(socketPath.begin(), socketPath.end(), address.sun_path);
    address.sun_path[socketPath.size()] = '\0';
    unlink(socketPath.c_str());
    if (bind(listeningSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        NIX_ERROR("bind failed");
        close(listeningSocket);
        return 1;
    }
    chmod(socketPath.c_str(), S_IRUSR | S_IWUSR);
    if (listen(listeningSocket, 64) == -1) {
        NIX_ERROR("listen failed");
        close(listeningSocket);
        unlink(socketPath.c_str());
        return 1;
    }

    logEvent("listening on " + socketPath + " (control plane with opt-in topic payload sniffing)");
    while (running.load()) {
        const int clientSocket = accept(listeningSocket, nullptr, nullptr);
        if (clientSocket == -1) {
            if (errno == EINTR) continue;
            if (!running.load()) break;
            NIX_ERROR("accept failed");
            continue;
        }
        std::thread(handleClient, clientSocket).detach();
    }
    if (listeningSocket != -1) close(listeningSocket);
    unlink(socketPath.c_str());
    return 0;
}