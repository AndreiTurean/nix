#include <nix/shm_channel.hxx>
#include <cassert>
#include <iostream>
#include <cstdint>
#include <string>
#include <string_view>
#include <sys/wait.h>
#include <time.h>
#include <array>
#include <atomic>
#include <thread>

std::int32_t main() {
    // Use a unique topic name for testing
    constexpr std::string_view testChannelName = "/nix_test_channel_ipc";
    constexpr std::uint64_t testCapacity = 8;

    // Ensure a clean slate in case a previous run crashed
    shm_unlink(std::string(testChannelName).c_str());

    {
        // 1. Create the shared memory channel (acts as Producer/Server)
        nix::ShmChannel<std::int32_t, testCapacity> producerChannel(testChannelName, nix::ShmMode::Create);
        const auto subscriberId = producerChannel.addSubscriber();

        bool mismatchedTypeRejected = false;
        try {
            nix::ShmChannel<float, testCapacity> mismatchedConsumer(testChannelName, nix::ShmMode::Open);
        } catch (const std::runtime_error&) {
            mismatchedTypeRejected = true;
        }
        assert(mismatchedTypeRejected);

        assert(producerChannel.empty(subscriberId) == true);
        assert(producerChannel.size(subscriberId) == 0);
        assert(producerChannel.capacity() == testCapacity);

        // 2. Push data into the channel
        assert(producerChannel.push(42) == true);
        assert(producerChannel.push(84) == true);
        assert(producerChannel.size(subscriberId) == 2);

        {
            // 3. Open the SAME shared memory segment from another handle (acts as Consumer/Client)
            nix::ShmChannel<std::int32_t, testCapacity> consumerChannel(testChannelName, nix::ShmMode::Open);

            assert(consumerChannel.empty(subscriberId) == false);
            assert(consumerChannel.size(subscriberId) == 2);

            // 4. Pop data from the consumer side
            auto val1 = consumerChannel.pop(subscriberId);
            assert(val1.has_value() && *val1 == 42);

            auto val2 = consumerChannel.pop(subscriberId);
            assert(val2.has_value() && *val2 == 84);

            assert(consumerChannel.empty(subscriberId) == true);

            int readyPipe[2];
            assert(pipe(readyPipe) == 0);
            const pid_t child = fork();
            assert(child != -1);
            if (child == 0) {
                close(readyPipe[0]);
                alarm(5);
                nix::ShmChannel<std::int32_t, testCapacity> blockingConsumer(testChannelName, nix::ShmMode::Open);
                const char ready = 'R';
                if (write(readyPipe[1], &ready, sizeof(ready)) != sizeof(ready)) {
                    _exit(1);
                }
                const auto received = blockingConsumer.waitPop(subscriberId);
                _exit(received.has_value() && *received == 123 ? 0 : 2);
            }

            close(readyPipe[1]);
            char ready = 0;
            assert(read(readyPipe[0], &ready, sizeof(ready)) == sizeof(ready));
            close(readyPipe[0]);
            const timespec waitTime{0, 50000000};
            nanosleep(&waitTime, nullptr);
            assert(producerChannel.push(123) == true);
            int childStatus = 0;
            assert(waitpid(child, &childStatus, 0) == child);
            assert(WIFEXITED(childStatus) && WEXITSTATUS(childStatus) == 0);

            // Push from consumer side back to producer view to test bidirectional flow if needed
            assert(consumerChannel.push(999) == true);
        } // consumerChannel goes out of scope and detaches (munmap), but backing shm remains alive

        // 5. Verify the data pushed by the consumer is visible to the producer
        auto val3 = producerChannel.pop(subscriberId);
        assert(val3.has_value() && *val3 == 999);
        assert(producerChannel.empty(subscriberId) == true);

    } // producerChannel goes out of scope, unlinks (shm_unlink), and cleans up system resources

    constexpr std::string_view concurrentChannelName = "/nix_test_channel_mpmc";
    constexpr std::int32_t producerCount = 4;
    constexpr std::int32_t messagesPerProducer = 1000;
    constexpr std::int32_t totalMessages = producerCount * messagesPerProducer;
    constexpr std::uint64_t concurrentCapacity = 64;
    shm_unlink(std::string(concurrentChannelName).c_str());
    {
        nix::ShmChannel<std::int32_t, concurrentCapacity> channel(concurrentChannelName, nix::ShmMode::Create);
        const auto subscriberId = channel.addSubscriber();
        std::array<std::atomic<std::int32_t>, totalMessages> seen{};
        std::atomic<std::int32_t> receivedCount{0};
        std::array<std::thread, producerCount> producers;
        std::array<std::thread, producerCount> consumers;

        for (std::int32_t producer = 0; producer < producerCount; ++producer) {
            producers[static_cast<std::size_t>(producer)] = std::thread([&, producer] {
                nix::ShmChannel<std::int32_t, concurrentCapacity> producerChannel(concurrentChannelName, nix::ShmMode::Open);
                for (std::int32_t sequence = 0; sequence < messagesPerProducer; ++sequence) {
                    const auto message = producer * messagesPerProducer + sequence;
                    while (!producerChannel.push(message)) {
                        std::this_thread::yield();
                    }
                }
            });
        }

        for (auto& consumer : consumers) {
            consumer = std::thread([&] {
                nix::ShmChannel<std::int32_t, concurrentCapacity> consumerChannel(concurrentChannelName, nix::ShmMode::Open);
                while (receivedCount.load(std::memory_order_relaxed) < totalMessages) {
                    const auto message = consumerChannel.pop(subscriberId);
                    if (message.has_value()) {
                        assert(*message >= 0 && *message < totalMessages);
                        seen[static_cast<std::size_t>(*message)].fetch_add(1, std::memory_order_relaxed);
                        receivedCount.fetch_add(1, std::memory_order_relaxed);
                    } else {
                        std::this_thread::yield();
                    }
                }
            });
        }

        for (auto& producer : producers) {
            producer.join();
        }
        for (auto& consumer : consumers) {
            consumer.join();
        }
        for (const auto& count : seen) {
            assert(count.load(std::memory_order_relaxed) == 1);
        }
        assert(channel.empty(subscriberId));
    }

    std::cout << "ShmChannel IPC test passed successfully!" << '\n';
    return 0;
}
