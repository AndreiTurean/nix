#pragma once

#include <nix/daemon.hxx>
#include <nix/shm_channel.hxx>
#include <memory>
#include <string>
#include <string_view>
#include <typeinfo>
#include <utility>

namespace nix {

template <typename T, std::uint64_t Capacity>
class Publisher {
public:
    Publisher(std::shared_ptr<DaemonClient> daemonClient, std::string_view topicName)
        : _daemonClient(std::move(daemonClient)), _topicName(topicName), _channel(topicName, ShmMode::Create) {}

    ~Publisher() {
        _daemonClient->unregisterEndpoint(_topicName, typeid(T).name(), EndpointRole::Publisher, Capacity);
    }

    /// Publishes once to the shared channel; each active subscriber can receive the message once.
    bool publish(const T& message) {
        const bool published = _channel.pushIfSubscribed(message);
        if (published && _channel.sniffEnabled()) {
            const std::string_view payload(reinterpret_cast<const char*>(&message), sizeof(message));
            _daemonClient->publishSample(_topicName, typeid(T).name(), payload, sizeof(message));
        }
        return published;
    }

    std::uint64_t capacity() const {
        return _channel.capacity();
    }

private:
    std::shared_ptr<DaemonClient> _daemonClient;
    std::string _topicName;
    ShmChannel<T, Capacity> _channel;
};

} // namespace nix
