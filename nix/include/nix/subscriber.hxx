#pragma once

#include <nix/shm_channel.hxx>
#include <nix/daemon.hxx>
#include <memory>
#include <string_view>
#include <optional>
#include <typeinfo>
#include <utility>

namespace nix {

template <typename T, std::uint64_t Capacity>
class Subscriber {
public:
    Subscriber(std::shared_ptr<DaemonClient> daemonClient, std::string_view topicName)
        : _daemonClient(std::move(daemonClient)), _topicName(topicName), _channel(topicName, ShmMode::Open) {
        _channel.addSubscriber();
    }

    ~Subscriber() {
        _channel.removeSubscriber();
        _daemonClient->unregisterEndpoint(_topicName, typeid(T).name(), EndpointRole::Subscriber, Capacity);
    }

    std::optional<T> receive() {
        return _channel.pop();
    }

    std::optional<T> receiveBlocking() {
        return _channel.waitPop();
    }

    bool empty() const {
        return _channel.empty();
    }

    std::uint64_t size() const {
        return _channel.size();
    }

private:
    std::shared_ptr<DaemonClient> _daemonClient;
    std::string _topicName;
    ShmChannel<T, Capacity> _channel;
};

} // namespace nix
