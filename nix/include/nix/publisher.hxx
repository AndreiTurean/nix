#pragma once

#include <nix/daemon.hxx>
#include <nix/shm_channel.hxx>
#include <algorithm>
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

    bool publish(const T& message) {
        const auto endpoints = _daemonClient->discover(_topicName);
        const bool hasSubscribers = std::any_of(endpoints.begin(), endpoints.end(), [](const EndpointInfo& endpoint) {
            return endpoint.role == EndpointRole::Subscriber;
        });
        if (!hasSubscribers) {
            _channel.discardPending();
            return true;
        }
        return _channel.push(message);
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
