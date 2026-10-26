#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace nix {

enum class EndpointRole {
    Publisher,
    Subscriber,
};

struct EndpointInfo {
    std::string nodeName;
    std::string topicName;
    std::string typeName;
    EndpointRole role;
    std::uint64_t capacity;
};

class DaemonClient {
public:
    explicit DaemonClient(std::string_view nodeName);
    ~DaemonClient();

    DaemonClient(const DaemonClient&) = delete;
    DaemonClient& operator=(const DaemonClient&) = delete;
    DaemonClient(DaemonClient&&) = delete;
    DaemonClient& operator=(DaemonClient&&) = delete;

    void registerEndpoint(std::string_view topicName, std::string_view typeName,
        EndpointRole role, std::uint64_t capacity);
    void unregisterEndpoint(std::string_view topicName, std::string_view typeName,
        EndpointRole role, std::uint64_t capacity) noexcept;
    std::vector<EndpointInfo> discover(std::string_view topicName = {});

private:
    int _socket;
    std::mutex _mutex;
};

std::string defaultDaemonSocketPath();

} // namespace nix