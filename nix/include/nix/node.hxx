#pragma once

#include <nix/publisher.hxx>
#include <nix/subscriber.hxx>
#include <nix/daemon.hxx>
#include <string>
#include <string_view>
#include <memory>
#include <typeinfo>

namespace nix {

class Node {
public:
    explicit Node(std::string_view nodeName)
        : _nodeName(nodeName), _daemonClient(std::make_shared<DaemonClient>(nodeName)) {}

    // Factory method to create a Publisher tied to this Node
    template <typename T, std::uint64_t Capacity>
    std::unique_ptr<Publisher<T, Capacity>> createPublisher(std::string_view topicName) {
        auto publisher = std::make_unique<Publisher<T, Capacity>>(_daemonClient, topicName);
        _daemonClient->registerEndpoint(topicName, typeid(T).name(), EndpointRole::Publisher, Capacity);
        return publisher;
    }

    // Factory method to create a Subscriber tied to this Node
    template <typename T, std::uint64_t Capacity>
    std::unique_ptr<Subscriber<T, Capacity>> createSubscriber(std::string_view topicName) {
        auto subscriber = std::make_unique<Subscriber<T, Capacity>>(_daemonClient, topicName);
        _daemonClient->registerEndpoint(topicName, typeid(T).name(), EndpointRole::Subscriber, Capacity);
        return subscriber;
    }

    std::vector<EndpointInfo> discover(std::string_view topicName = {}) {
        return _daemonClient->discover(topicName);
    }

    std::string_view name() const {
        return _nodeName;
    }

private:
    std::string _nodeName;
    std::shared_ptr<DaemonClient> _daemonClient;
};

} // namespace nix
