#include <nix/nix.hxx>
#include <nix/serialized_message.hxx>

#include <bit>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

int main(int argc, char** argv) {
    constexpr std::string_view defaultTopicName = "/nix_example_counter";
    constexpr std::uint64_t capacity = 16;
    const std::string_view topicName = argc > 1 ? argv[1] : defaultTopicName;
    const std::int32_t messageLimit = argc > 2 ? std::stoi(argv[2]) : -1;
    nix::Node node("counter_subscriber");

    for (;;) {
        const auto endpoints = node.discover(topicName);
        bool publisherAvailable = false;
        for (const auto& endpoint : endpoints) {
            if (endpoint.role == nix::EndpointRole::Publisher
                && endpoint.typeName == typeid(nix::SerializedMessage).name()) {
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
            auto subscriber = node.createSubscriber<nix::SerializedMessage, capacity>(topicName);
            std::cout << "listening on " << topicName << std::endl;
            std::int32_t receivedCount = 0;
            for (;;) {
                const auto message = subscriber->receiveBlocking();
                if (message.has_value()) {
                    const auto frame = nix::decodeSerializedMessage(*message);
                    if (frame.schema != "nix.counter.int32/v1"
                        || frame.encoding != "application/x-nix-int32-le"
                        || frame.payload.size() != 4U) {
                        std::cerr << "unsupported serialized counter frame\n";
                        return 1;
                    }
                    const std::uint32_t bits = static_cast<std::uint32_t>(frame.payload[0])
                        | (static_cast<std::uint32_t>(frame.payload[1]) << 8U)
                        | (static_cast<std::uint32_t>(frame.payload[2]) << 16U)
                        | (static_cast<std::uint32_t>(frame.payload[3]) << 24U);
                    std::cout << "received: " << std::bit_cast<std::int32_t>(bits) << std::endl;
                    ++receivedCount;
                    if (messageLimit >= 0 && receivedCount >= messageLimit) {
                        return 0;
                    }
                }
            }
        } catch (const std::system_error& error) {
            std::cerr << "publisher disappeared while connecting: " << error.what() << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    }
}