#pragma once

#include <nix/publisher.hxx>
#include <nix/subscriber.hxx>
#include <string>
#include <string_view>
#include <memory>

namespace nix {

class Node {
public:
    explicit Node(std::string_view nodeName)
        : _nodeName(nodeName) {}

    // Factory method to create a Publisher tied to this Node
    template <typename T, std::uint64_t Capacity>
    std::unique_ptr<Publisher<T, Capacity>> createPublisher(std::string_view topicName) {
        // TODO: Register topicName and _nodeName with the Central Manager here!
        return std::make_unique<Publisher<T, Capacity>>(topicName);
    }

    // Factory method to create a Subscriber tied to this Node
    template <typename T, std::uint64_t Capacity>
    std::unique_ptr<Subscriber<T, Capacity>> createSubscriber(std::string_view topicName) {
        // TODO: Register subscription intent with the Central Manager here!
        return std::make_unique<Subscriber<T, Capacity>>(topicName);
    }

    std::string_view name() const {
        return _nodeName;
    }

private:
    std::string _nodeName;
};

} // namespace nix
