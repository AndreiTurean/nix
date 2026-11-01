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

struct TopicSample {
    std::string nodeName;
    std::string topicName;
    std::string typeName;
    std::uint64_t publishedAtMillis;
    std::uint64_t originalSize;
    std::string payload;
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
    void setSniffTopic(std::string_view topicName);
    void publishSample(std::string_view topicName, std::string_view typeName,
        std::string_view payload, std::uint64_t originalSize) noexcept;
    std::vector<TopicSample> sniff(std::string_view topicName = {});

private:
    int _socket;
    std::mutex _mutex;
};

std::string defaultDaemonSocketPath();

} // namespace nix