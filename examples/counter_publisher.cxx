#include <nix/nix.hxx>
#include <nix/serialized_message.hxx>

#include <array>
#include <bit>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

int main(int argc, char** argv) {
    constexpr std::string_view defaultTopicName = "/nix_example_counter";
    constexpr std::uint64_t capacity = 16;
    const std::string_view topicName = argc > 1 ? argv[1] : defaultTopicName;
    const std::int32_t messageLimit = argc > 2 ? std::stoi(argv[2]) : -1;
    try {
        nix::Node node("counter_publisher");
        auto publisher = node.createPublisher<nix::SerializedMessage, capacity>(topicName);

        std::int32_t counter = 0;
        std::int32_t publishedCount = 0;
        for (;;) {
            const std::uint32_t bits = std::bit_cast<std::uint32_t>(counter);
            const std::array<std::uint8_t, 4> payload{
                static_cast<std::uint8_t>(bits & 0xffU),
                static_cast<std::uint8_t>((bits >> 8U) & 0xffU),
                static_cast<std::uint8_t>((bits >> 16U) & 0xffU),
                static_cast<std::uint8_t>((bits >> 24U) & 0xffU),
            };
            const auto message = nix::makeSerializedMessage(
                "nix.counter.int32/v1", "application/x-nix-int32-le", payload.data(), payload.size());
            if (publisher->publish(message)) {
                std::cout << "published: " << counter << std::endl;
                counter = counter == std::numeric_limits<std::int32_t>::max() ? 0 : counter + 1;
                ++publishedCount;
                if (messageLimit >= 0 && publishedCount >= messageLimit) {
                    return 0;
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
    } catch (const std::system_error& error) {
        std::cerr << "counter publisher startup failed: " << error.what() << '\n'
            << "Make sure nix-daemon is running and remove a stale topic with nix-cleanup-shm --apply.\n";
        return 1;
    }
}