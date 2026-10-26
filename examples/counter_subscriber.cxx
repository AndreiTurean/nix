#include <nix/nix.hxx>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

int main() {
    constexpr char topicName[] = "/nix_example_counter";
    constexpr std::uint64_t capacity = 16;
    nix::Node node("counter_subscriber");

    for (;;) {
        const auto endpoints = node.discover(topicName);
        bool publisherAvailable = false;
        for (const auto& endpoint : endpoints) {
            if (endpoint.role == nix::EndpointRole::Publisher
                && endpoint.typeName == typeid(std::int32_t).name()) {
                publisherAvailable = true;
                break;
            }
        }
        if (!publisherAvailable) {
            std::cout << "waiting for counter publisher..." << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        try {
            auto subscriber = node.createSubscriber<std::int32_t, capacity>(topicName);
            std::cout << "listening on " << topicName << std::endl;
            for (;;) {
                const auto value = subscriber->receiveBlocking();
                if (value.has_value()) {
                    std::cout << "received: " << *value << std::endl;
                }
            }
        } catch (const std::system_error& error) {
            std::cerr << "publisher disappeared while connecting: " << error.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
}