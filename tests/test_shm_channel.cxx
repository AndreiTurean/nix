#include <nix/shm_channel.hxx>
#include <cassert>
#include <iostream>
#include <cstdint>
#include <string_view>

std::int32_t main() {
    // Use a unique topic name for testing
    constexpr std::string_view testChannelName = "/nix_test_channel_ipc";
    constexpr std::uint64_t testCapacity = 8;

    // Ensure a clean slate in case a previous run crashed
    shm_unlink(testChannelName.data());

    {
        // 1. Create the shared memory channel (acts as Producer/Server)
        nix::ShmChannel<std::int32_t, testCapacity> producerChannel(testChannelName, nix::ShmMode::Create);

        assert(producerChannel.empty() == true);
        assert(producerChannel.size() == 0);
        assert(producerChannel.capacity() == testCapacity);

        // 2. Push data into the channel
        assert(producerChannel.push(42) == true);
        assert(producerChannel.push(84) == true);
        assert(producerChannel.size() == 2);

        {
            // 3. Open the SAME shared memory segment from another handle (acts as Consumer/Client)
            nix::ShmChannel<std::int32_t, testCapacity> consumerChannel(testChannelName, nix::ShmMode::Open);

            assert(consumerChannel.empty() == false);
            assert(consumerChannel.size() == 2);

            // 4. Pop data from the consumer side
            auto val1 = consumerChannel.pop();
            assert(val1.has_value() && *val1 == 42);

            auto val2 = consumerChannel.pop();
            assert(val2.has_value() && *val2 == 84);

            assert(consumerChannel.empty() == true);

            // Push from consumer side back to producer view to test bidirectional flow if needed
            assert(consumerChannel.push(999) == true);
        } // consumerChannel goes out of scope and detaches (munmap), but backing shm remains alive

        // 5. Verify the data pushed by the consumer is visible to the producer
        auto val3 = producerChannel.pop();
        assert(val3.has_value() && *val3 == 999);
        assert(producerChannel.empty() == true);

    } // producerChannel goes out of scope, unlinks (shm_unlink), and cleans up system resources

    std::cout << "ShmChannel IPC test passed successfully!" << std::endl;
    return 0;
}
