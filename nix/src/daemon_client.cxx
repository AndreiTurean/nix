#include <nix/daemon.hxx>

#include <cerrno>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace nix {
namespace {

constexpr std::size_t maxLineLength = 1024U * 1024U;
constexpr char hexDigits[] = "0123456789abcdef";

std::string encode(std::string_view value) {
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
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

std::string decode(std::string_view value) {
    if ((value.size() % 2U) != 0U) {
        throw std::runtime_error("invalid hex field from nix-daemon");
    }
    std::string result;
    result.reserve(value.size() / 2U);
    for (std::size_t index = 0; index < value.size(); index += 2U) {
        const int high = decodeHexDigit(value[index]);
        const int low = decodeHexDigit(value[index + 1U]);
        if (high < 0 || low < 0) {
            throw std::runtime_error("invalid hex field from nix-daemon");
        }
        result.push_back(static_cast<char>((high << 4) | low));
    }
    return result;
}

void sendLine(int socket, std::string_view line) {
    std::string message(line);
    message.push_back('\n');
    std::size_t sent = 0;
    while (sent < message.size()) {
        const ssize_t result = send(socket, message.data() + sent, message.size() - sent, MSG_NOSIGNAL);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "sending nix-daemon request failed");
        }
        if (result == 0) {
            throw std::runtime_error("nix-daemon closed the connection");
        }
        sent += static_cast<std::size_t>(result);
    }
}

std::string readLine(int socket) {
    std::string line;
    line.reserve(128U);
    while (line.size() < maxLineLength) {
        char character = 0;
        const ssize_t result = recv(socket, &character, sizeof(character), 0);
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::system_error(errno, std::generic_category(), "reading nix-daemon response failed");
        }
        if (result == 0) {
            throw std::runtime_error("nix-daemon closed the connection");
        }
        if (character == '\n') {
            return line;
        }
        line.push_back(character);
    }
    throw std::runtime_error("nix-daemon response exceeded the maximum line length");
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

std::uint64_t parseCapacity(std::string_view value) {
    std::uint64_t capacity = 0;
    for (const char digit : value) {
        if (digit < '0' || digit > '9') {
            throw std::runtime_error("invalid capacity from nix-daemon");
        }
        const std::uint64_t numericDigit = static_cast<std::uint64_t>(digit - '0');
        if (capacity > (std::numeric_limits<std::uint64_t>::max() - numericDigit) / 10U) {
            throw std::runtime_error("capacity overflow from nix-daemon");
        }
        capacity = capacity * 10U + numericDigit;
    }
    return capacity;
}

} // namespace

std::string defaultDaemonSocketPath() {
    if (const char* configuredPath = std::getenv("NIX_DAEMON_SOCKET"); configuredPath != nullptr && configuredPath[0] != '\0') {
        return configuredPath;
    }
    return "/tmp/nix-daemon-" + std::to_string(static_cast<unsigned long long>(getuid())) + ".sock";
}

DaemonClient::DaemonClient(std::string_view nodeName) : _socket(-1) {
    const std::string socketPath = defaultDaemonSocketPath();
    if (socketPath.size() >= sizeof(sockaddr_un::sun_path)) {
        throw std::invalid_argument("nix-daemon socket path is too long");
    }

    _socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if (_socket == -1) {
        throw std::system_error(errno, std::generic_category(), "creating nix-daemon socket failed");
    }

    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::copy(socketPath.begin(), socketPath.end(), address.sun_path);
    address.sun_path[socketPath.size()] = '\0';
    if (connect(_socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == -1) {
        const int savedError = errno;
        close(_socket);
        _socket = -1;
        throw std::system_error(savedError, std::generic_category(), "connecting to nix-daemon failed");
    }

    try {
        sendLine(_socket, "HELLO\t" + encode(nodeName));
        const std::string response = readLine(_socket);
        if (response != "OK") {
            throw std::runtime_error("nix-daemon rejected node registration: " + response);
        }
    } catch (...) {
        close(_socket);
        _socket = -1;
        throw;
    }
}

DaemonClient::~DaemonClient() {
    if (_socket != -1) {
        close(_socket);
    }
}

void DaemonClient::registerEndpoint(std::string_view topicName, std::string_view typeName,
    EndpointRole role, std::uint64_t capacity) {
    std::lock_guard<std::mutex> lock(_mutex);
    const char* roleName = role == EndpointRole::Publisher ? "P" : "S";
    sendLine(_socket, "REGISTER\t" + std::string(roleName) + "\t" + std::to_string(capacity)
        + "\t" + encode(topicName) + "\t" + encode(typeName));
    const std::string response = readLine(_socket);
    if (response != "OK") {
        throw std::runtime_error("nix-daemon rejected endpoint registration: " + response);
    }
}

std::vector<EndpointInfo> DaemonClient::discover(std::string_view topicName) {
    std::lock_guard<std::mutex> lock(_mutex);
    sendLine(_socket, "DISCOVER\t" + (topicName.empty() ? std::string("-") : encode(topicName)));

    std::vector<EndpointInfo> endpoints;
    for (;;) {
        const std::string response = readLine(_socket);
        if (response == "END") {
            return endpoints;
        }
        const auto fields = split(response);
        if (fields.size() != 6U || fields[0] != "ENTRY") {
            throw std::runtime_error("invalid discovery response from nix-daemon");
        }
        EndpointRole role;
        if (fields[1] == "P") {
            role = EndpointRole::Publisher;
        } else if (fields[1] == "S") {
            role = EndpointRole::Subscriber;
        } else {
            throw std::runtime_error("invalid endpoint role from nix-daemon");
        }
        endpoints.push_back(EndpointInfo{
            decode(fields[2]), decode(fields[3]), decode(fields[4]), role, parseCapacity(fields[5])
        });
    }
}

} // namespace nix