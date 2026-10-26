#include <nix/daemon.hxx>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <sys/socket.h>
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

std::mutex registryMutex;
std::mutex logMutex;
std::vector<Registration> registry;
std::atomic<bool> running{true};
int listeningSocket = -1;

void logEvent(const std::string& message) {
    std::lock_guard<std::mutex> lock(logMutex);
    std::cout << "nix-daemon: " << message << std::endl;
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
    result.reserve(value.size() * 2U);
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
        result.push_back(static_cast<char>((high << 4) | low));
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
    while (line.size() < 1024U * 1024U) {
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
                }
                sessionRegistrations.push_back(std::move(registration));
                const Registration& registered = sessionRegistrations.back();
                const std::string roleName = registered.role == nix::EndpointRole::Publisher ? "publisher" : "subscriber";
                logEvent("registered " + roleName + " node=" + registered.nodeName
                    + " topic=" + registered.topicName + " type=" + registered.typeName
                    + " capacity=" + std::to_string(registered.capacity));
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
            } else {
                if (!sendLine(clientSocket, "ERROR\tunknown request")) break;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << "nix-daemon client error: " << error.what() << '\n';
    }

    {
        std::lock_guard<std::mutex> lock(registryMutex);
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
        std::cerr << "Usage: nix-daemon [--socket PATH]\n";
        return 2;
    }
    if (socketPath.size() >= sizeof(sockaddr_un::sun_path)) {
        std::cerr << "nix-daemon socket path is too long\n";
        return 2;
    }

    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    listeningSocket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listeningSocket == -1) {
        std::cerr << "nix-daemon: socket creation failed\n";
        return 1;
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::copy(socketPath.begin(), socketPath.end(), address.sun_path);
    address.sun_path[socketPath.size()] = '\0';
    unlink(socketPath.c_str());
    if (bind(listeningSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        std::cerr << "nix-daemon: bind failed\n";
        close(listeningSocket);
        return 1;
    }
    chmod(socketPath.c_str(), S_IRUSR | S_IWUSR);
    if (listen(listeningSocket, 64) == -1) {
        std::cerr << "nix-daemon: listen failed\n";
        close(listeningSocket);
        unlink(socketPath.c_str());
        return 1;
    }

    logEvent("listening on " + socketPath + " (control plane only; payloads use shared memory)");
    while (running.load()) {
        const int clientSocket = accept(listeningSocket, nullptr, nullptr);
        if (clientSocket == -1) {
            if (errno == EINTR) continue;
            if (!running.load()) break;
            std::cerr << "nix-daemon: accept failed\n";
            continue;
        }
        std::thread(handleClient, clientSocket).detach();
    }
    if (listeningSocket != -1) close(listeningSocket);
    unlink(socketPath.c_str());
    return 0;
}