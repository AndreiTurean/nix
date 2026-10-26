#include <nix/nix.hxx>
#include <cassert>
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

struct TestMessage {
    std::uint64_t sequenceId;
    std::int32_t payload;
};

class DaemonProcess {
public:
    explicit DaemonProcess(const char* executablePath, std::string_view suffix = {})
        : _executablePath(executablePath),
          _socketPath("/tmp/nix-daemon-test-" + std::to_string(static_cast<long long>(getpid()))
              + std::string(suffix) + ".sock"), _process(-1) {
        if (setenv("NIX_DAEMON_SOCKET", _socketPath.c_str(), 1) != 0) {
            throw std::runtime_error("failed to configure nix-daemon test socket");
        }

        start();
    }

    void stop() {
        if (_process != -1) {
            kill(_process, SIGTERM);
            waitpid(_process, nullptr, 0);
            _process = -1;
        }
    }

    ~DaemonProcess() {
        stop();
        unlink(_socketPath.c_str());
    }

    DaemonProcess(const DaemonProcess&) = delete;
    DaemonProcess& operator=(const DaemonProcess&) = delete;

private:
    void start() {
        _process = fork();
        if (_process == -1) {
            throw std::runtime_error("failed to fork nix-daemon test process");
        }
        if (_process == 0) {
            execl(_executablePath.c_str(), _executablePath.c_str(), "--socket", _socketPath.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }

        const timespec retryDelay{0, 10000000};
        for (int attempt = 0; attempt < 100; ++attempt) {
            const int probeSocket = socket(AF_UNIX, SOCK_STREAM, 0);
            if (probeSocket == -1) {
                throw std::runtime_error("failed to create nix-daemon readiness socket");
            }
            sockaddr_un address{};
            address.sun_family = AF_UNIX;
            std::copy(_socketPath.begin(), _socketPath.end(), address.sun_path);
            address.sun_path[_socketPath.size()] = '\0';
            const int connectResult = connect(probeSocket, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
            close(probeSocket);
            if (connectResult == 0) {
                return;
            }
            nanosleep(&retryDelay, nullptr);
        }
        throw std::runtime_error("nix-daemon test process did not start");
    }

    std::string _executablePath;
    std::string _socketPath;
    pid_t _process;
};

int main(int argc, char** argv) {
    assert(argc == 2);
    DaemonProcess daemon(argv[1]);
    constexpr std::string_view topicName = "/nix_pub_sub_test_topic";
    constexpr std::uint64_t capacity = 16; // Must be a power of 2

    // Ensure a clean slate
    shm_unlink(std::string(topicName).c_str());

    {
        // 1. Initialize our central nodes
        nix::Node producerNode("producer_process");
        nix::Node subscriberNode("consumer_process");

        // 2. Create publisher and subscriber endpoints via node factories
        auto pub = producerNode.createPublisher<TestMessage, capacity>(topicName);

        // Messages published before any subscriber exists are intentionally discarded.
        assert(pub->publish(TestMessage{999, -1}) == true);

        auto sub = subscriberNode.createSubscriber<TestMessage, capacity>(topicName);

        const auto discovered = producerNode.discover(topicName);
        assert(discovered.size() == 2);
        assert(discovered[0].topicName == topicName);
        assert(discovered[1].topicName == topicName);
        assert(discovered[0].typeName == typeid(TestMessage).name());
        assert(discovered[1].typeName == typeid(TestMessage).name());
        assert(producerNode.discover("/unknown_topic").empty());

        // 3. Verify initial states
        assert(sub->empty() == true);
        assert(sub->size() == 0);
        assert(sub->receive().has_value() == false);

        // 4. Send a burst of messages and verify no drops occur under normal capacity
        constexpr std::int32_t totalMessages = 10;
        for (std::int32_t i = 0; i < totalMessages; ++i) {
            TestMessage msg{static_cast<std::uint64_t>(i), i * 10};
            bool success = pub->publish(msg);
            assert(success == true);
        }

        // Verify size matches the total pushed items
        assert(sub->size() == totalMessages);
        assert(sub->empty() == false);

        // 5. Receive and validate FIFO ordering and zero data loss
        for (std::int32_t i = 0; i < totalMessages; ++i) {
            auto received = sub->receive();
            assert(received.has_value());
            assert(received->sequenceId == static_cast<std::uint64_t>(i));
            assert(received->payload == i * 10);
        }

        // 6. Verify channel is empty again after consumption
        assert(sub->empty() == true);
        assert(sub->size() == 0);

        sub.reset();
        assert(pub->publish(TestMessage{777, 777}) == true);
        sub = subscriberNode.createSubscriber<TestMessage, capacity>(topicName);
        assert(sub->empty() == true);
        assert(sub->receive().has_value() == false);

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

    {
        nix::Node observerNode("observer_process");
        assert(observerNode.discover(topicName).empty());
    }

    {
        DaemonProcess disconnectedDaemon(argv[1], "-disconnected");
        nix::Node producerNode("disconnected_producer");
        nix::Node subscriberNode("disconnected_consumer");
        constexpr std::string_view disconnectedTopic = "/nix_pub_sub_disconnected_topic";
        constexpr std::uint64_t disconnectedCapacity = 2;
        shm_unlink(std::string(disconnectedTopic).c_str());
        auto publisher = producerNode.createPublisher<TestMessage, disconnectedCapacity>(disconnectedTopic);
        auto subscriber = subscriberNode.createSubscriber<TestMessage, disconnectedCapacity>(disconnectedTopic);

        disconnectedDaemon.stop();
        assert(publisher->publish(TestMessage{1, 10}));
        auto received = subscriber->receive();
        assert(received.has_value() && received->sequenceId == 1);
    }

    std::cout << "Publisher-Subscriber zero-loss IPC test passed successfully!" << '\n';
    return 0;
}
