#pragma once

#include <nix/shm_channel.hxx>
#include <nix/daemon.hxx>
#include <memory>
#include <string_view>
#include <optional>
#include <typeinfo>
#include <utility>

namespace nix {

/// Each subscriber has an independent cursor over messages in the shared channel.
template <typename T, std::uint64_t Capacity>
class Subscriber {
public:
    Subscriber(std::shared_ptr<DaemonClient> daemonClient, std::string_view topicName)
        : _daemonClient(std::move(daemonClient)), _topicName(topicName), _channel(topicName, ShmMode::Open) {
        _subscriberId = _channel.addSubscriber();
    }

    ~Subscriber() {
        _channel.removeSubscriber(_subscriberId);
        _daemonClient->unregisterEndpoint(_topicName, typeid(T).name(), EndpointRole::Subscriber, Capacity);
    }

    std::optional<T> receive() {
        return _channel.pop(_subscriberId);
    }

    std::optional<T> receiveBlocking() {
        return _channel.waitPop(_subscriberId);
    }

    bool empty() const {
        return _channel.empty(_subscriberId);
    }

    std::uint64_t size() const {
        return _channel.size(_subscriberId);
    }

private:
    std::shared_ptr<DaemonClient> _daemonClient;
    std::string _topicName;
    ShmChannel<T, Capacity> _channel;
    typename ShmChannel<T, Capacity>::SubscriberId _subscriberId = 0;
};

} // namespace nix
