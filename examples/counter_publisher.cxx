#include <nix/nix.hxx>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <system_error>
#include <thread>

int main() {
    constexpr char topicName[] = "/nix_example_counter";
    constexpr std::uint64_t capacity = 16;
    try {
        nix::Node node("counter_publisher");
        auto publisher = node.createPublisher<std::int32_t, capacity>(topicName);

        std::int32_t counter = 0;
        for (;;) {
            if (publisher->publish(counter)) {
                std::cout << "published: " << counter << std::endl;
                counter = counter == std::numeric_limits<std::int32_t>::max() ? 0 : counter + 1;
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