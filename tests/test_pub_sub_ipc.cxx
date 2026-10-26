#include <nix/nix.hxx>
#include <cassert>
#include <iostream>
#include <cstdint>

struct TestMessage {
    std::uint64_t sequenceId;
    int payload;
};

int main() {
    constexpr std::string_view topicName = "/nix_pub_sub_test_topic";
    constexpr std::uint64_t capacity = 16; // Must be a power of 2

    // Ensure a clean slate
    shm_unlink(topicName.data());

    {
        // 1. Initialize our central nodes
        nix::Node producerNode("producer_process");
        nix::Node subscriberNode("consumer_process");

        // 2. Create publisher and subscriber endpoints via node factories
        auto pub = producerNode.createPublisher<TestMessage, capacity>(topicName);
        auto sub = subscriberNode.createSubscriber<TestMessage, capacity>(topicName);

        // 3. Verify initial states
        assert(sub->empty() == true);
        assert(sub->size() == 0);

        // 4. Send a burst of messages and verify no drops occur under normal capacity
        constexpr int totalMessages = 10;
        for (int i = 0; i < totalMessages; ++i) {
            TestMessage msg{static_cast<std::uint64_t>(i), i * 10};
            bool success = pub->publish(msg);
            assert(success == true);
        }

        // Verify size matches the total pushed items
        assert(sub->size() == totalMessages);
        assert(sub->empty() == false);

        // 5. Receive and validate FIFO ordering and zero data loss
        for (int i = 0; i < totalMessages; ++i) {
            auto received = sub->receive();
            assert(received.has_value());
            assert(received->sequenceId == static_cast<std::uint64_t>(i));
            assert(received->payload == i * 10);
        }

        // 6. Verify channel is empty again after consumption
        assert(sub->empty() == true);
        assert(sub->size() == 0);

        // 7. Test backpressure/overflow protection: fill up the buffer completely
        for (std::uint64_t i = 0; i < capacity; ++i) {
            assert(pub->publish(TestMessage{i, 99}) == true);
        }

        // Pushing one more when full should fail gracefully (return false) instead of overwriting
        assert(pub->publish(TestMessage{999, 999}) == false);

        // Clean out the buffer
        for (std::uint64_t i = 0; i < capacity; ++i) {
            auto msg = sub->receive();
            assert(msg.has_value());
        }
        assert(sub->empty() == true);
    }

    std::cout << "Publisher-Subscriber zero-loss IPC test passed successfully!" << std::endl;
    return 0;
}
