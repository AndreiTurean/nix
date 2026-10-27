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

    /// Publishes once to the topic's shared queue; subscribers compete to consume each message.
    /// A future broadcast mode would be separate and deliver an independent copy to each subscriber.
    bool publish(const T& message) {
        return _channel.pushIfSubscribed(message);
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
