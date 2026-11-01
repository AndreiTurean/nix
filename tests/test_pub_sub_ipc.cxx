#include <nix/nix.hxx>
#include <cassert>
#include <algorithm>
#include <iostream>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>
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
                            + std::string(suffix) + ".sock"), _process(-1),
                    _hadPreviousSocketPath(false), _previousSocketPath() {
                if (const char* previousSocketPath = std::getenv("NIX_DAEMON_SOCKET"); previousSocketPath != nullptr) {
                        _hadPreviousSocketPath = true;
                        _previousSocketPath = previousSocketPath;
                }
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

    void restart() {
        stop();
        start();
    }

    ~DaemonProcess() {
        stop();
        unlink(_socketPath.c_str());
        if (_hadPreviousSocketPath) {
            setenv("NIX_DAEMON_SOCKET", _previousSocketPath.c_str(), 1);
        } else {
            unsetenv("NIX_DAEMON_SOCKET");
        }
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
    bool _hadPreviousSocketPath;
    std::string _previousSocketPath;
};

bool waitForEndpoint(nix::Node& observer, std::string_view topic, const char* nodeName,
    nix::EndpointRole role, bool expected) {
    const timespec retryDelay{0, 10000000};
    for (int attempt = 0; attempt < 200; ++attempt) {
        const auto endpoints = observer.discover(topic);
        const bool found = std::any_of(endpoints.begin(), endpoints.end(), [&](const nix::EndpointInfo& endpoint) {
            return endpoint.nodeName == nodeName && endpoint.role == role;
        });
        if (found == expected) {
            return true;
        }
        nanosleep(&retryDelay, nullptr);
    }
    return false;
}

void killProcess(pid_t process) {
    assert(kill(process, SIGKILL) == 0);
    int status = 0;
    assert(waitpid(process, &status, 0) == process);
    assert(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
}

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
        constexpr std::string_view sharedQueueTopic = "/nix_pub_sub_shared_queue_topic";
        constexpr std::uint64_t sharedQueueCapacity = 8;
        shm_unlink(std::string(sharedQueueTopic).c_str());

        nix::Node producerNode("shared_queue_producer");
        nix::Node firstSubscriberNode("shared_queue_consumer_one");
        nix::Node secondSubscriberNode("shared_queue_consumer_two");
        auto publisher = producerNode.createPublisher<TestMessage, sharedQueueCapacity>(sharedQueueTopic);
        auto firstSubscriber = firstSubscriberNode.createSubscriber<TestMessage, sharedQueueCapacity>(sharedQueueTopic);
        auto secondSubscriber = secondSubscriberNode.createSubscriber<TestMessage, sharedQueueCapacity>(sharedQueueTopic);

        for (std::uint64_t sequence = 0; sequence < 4; ++sequence) {
            assert(publisher->publish(TestMessage{sequence, static_cast<std::int32_t>(sequence)}));
        }

        for (std::uint64_t sequence = 0; sequence < 4; ++sequence) {
            const auto firstReceived = firstSubscriber->receive();
            const auto secondReceived = secondSubscriber->receive();
            assert(firstReceived.has_value() && firstReceived->sequenceId == sequence);
            assert(secondReceived.has_value() && secondReceived->sequenceId == sequence);
        }

        for (std::uint64_t sequence = 4; sequence < 4 + sharedQueueCapacity; ++sequence) {
            assert(publisher->publish(TestMessage{sequence, static_cast<std::int32_t>(sequence)}));
        }
        assert(!publisher->publish(TestMessage{99, 99}));
        for (std::uint64_t sequence = 4; sequence < 4 + sharedQueueCapacity; ++sequence) {
            const auto firstReceived = firstSubscriber->receive();
            const auto secondReceived = secondSubscriber->receive();
            assert(firstReceived.has_value() && firstReceived->sequenceId == sequence);
            assert(secondReceived.has_value() && secondReceived->sequenceId == sequence);
        }
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

    {
        constexpr std::string_view crashedPublisherTopic = "/nix_pub_crash_recovery_topic";
        constexpr std::uint64_t crashCapacity = 4;
        shm_unlink(std::string(crashedPublisherTopic).c_str());

        nix::Node observerNode("publisher_crash_observer");
        const pid_t publisherProcess = fork();
        assert(publisherProcess != -1);
        if (publisherProcess == 0) {
            nix::Node crashedPublisherNode("crashed_publisher");
            auto crashedPublisher = crashedPublisherNode.createPublisher<TestMessage, crashCapacity>(crashedPublisherTopic);
            static_cast<void>(crashedPublisher);
            for (;;) pause();
        }
        assert(waitForEndpoint(observerNode, crashedPublisherTopic, "crashed_publisher",
            nix::EndpointRole::Publisher, true));
        killProcess(publisherProcess);
        assert(waitForEndpoint(observerNode, crashedPublisherTopic, "crashed_publisher",
            nix::EndpointRole::Publisher, false));

        nix::Node replacementPublisherNode("replacement_publisher");
        nix::Node subscriberNode("publisher_crash_subscriber");
        auto replacementPublisher = replacementPublisherNode.createPublisher<TestMessage, crashCapacity>(crashedPublisherTopic);
        auto subscriber = subscriberNode.createSubscriber<TestMessage, crashCapacity>(crashedPublisherTopic);
        assert(replacementPublisher->publish(TestMessage{7, 70}));
        const auto received = subscriber->receive();
        assert(received.has_value() && received->sequenceId == 7);
    }

    {
        constexpr std::string_view crashedSubscriberTopic = "/nix_sub_crash_recovery_topic";
        constexpr std::uint64_t crashCapacity = 4;
        shm_unlink(std::string(crashedSubscriberTopic).c_str());

        nix::Node publisherNode("subscriber_crash_publisher");
        nix::Node liveSubscriberNode("live_subscriber");
        nix::Node observerNode("subscriber_crash_observer");
        auto publisher = publisherNode.createPublisher<TestMessage, crashCapacity>(crashedSubscriberTopic);
        auto liveSubscriber = liveSubscriberNode.createSubscriber<TestMessage, crashCapacity>(crashedSubscriberTopic);

        const pid_t subscriberProcess = fork();
        assert(subscriberProcess != -1);
        if (subscriberProcess == 0) {
            nix::Node crashedSubscriberNode("crashed_subscriber");
            auto crashedSubscriber = crashedSubscriberNode.createSubscriber<TestMessage, crashCapacity>(crashedSubscriberTopic);
            static_cast<void>(crashedSubscriber);
            for (;;) pause();
        }
        assert(waitForEndpoint(observerNode, crashedSubscriberTopic, "crashed_subscriber",
            nix::EndpointRole::Subscriber, true));
        killProcess(subscriberProcess);
        assert(waitForEndpoint(observerNode, crashedSubscriberTopic, "crashed_subscriber",
            nix::EndpointRole::Subscriber, false));
        assert(waitForEndpoint(observerNode, crashedSubscriberTopic, "live_subscriber",
            nix::EndpointRole::Subscriber, true));

        for (std::uint64_t sequence = 0; sequence < crashCapacity + 2; ++sequence) {
            assert(publisher->publish(TestMessage{sequence, static_cast<std::int32_t>(sequence)}));
            const auto received = liveSubscriber->receive();
            assert(received.has_value() && received->sequenceId == sequence);
        }
    }

    {
        DaemonProcess restartedDaemon(argv[1], "-restart");
        constexpr std::string_view restartTopic = "/nix_daemon_restart_topic";
        constexpr std::uint64_t restartCapacity = 4;
        shm_unlink(std::string(restartTopic).c_str());

        nix::Node publisherNode("restart_existing_publisher");
        nix::Node subscriberNode("restart_existing_subscriber");
        auto publisher = publisherNode.createPublisher<TestMessage, restartCapacity>(restartTopic);
        auto subscriber = subscriberNode.createSubscriber<TestMessage, restartCapacity>(restartTopic);

        restartedDaemon.restart();
        assert(publisher->publish(TestMessage{11, 110}));
        const auto received = subscriber->receive();
        assert(received.has_value() && received->sequenceId == 11);

        nix::Node observerNode("restart_observer");
        assert(observerNode.discover(restartTopic).empty());
        nix::Node recoveredNode("restart_recovered_node");
        auto recoveredSubscriber = recoveredNode.createSubscriber<TestMessage, restartCapacity>(restartTopic);
        const auto endpoints = observerNode.discover(restartTopic);
        assert(endpoints.size() == 1);
        assert(endpoints[0].nodeName == "restart_recovered_node");
        assert(publisher->publish(TestMessage{12, 120}));
        const auto recoveredMessage = recoveredSubscriber->receive();
        assert(recoveredMessage.has_value() && recoveredMessage->sequenceId == 12);
    }

    std::cout << "Publisher-Subscriber zero-loss IPC test passed successfully!" << '\n';
    return 0;
}
