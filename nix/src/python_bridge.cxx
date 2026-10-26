#include <nix/node.hxx>

#include <cstdint>
#include <exception>
#include <memory>
#include <string>

namespace {

constexpr std::uint64_t pythonChannelCapacity = 16;

thread_local std::string lastError;

struct Int32PublisherHandle {
    nix::Node node;
    std::unique_ptr<nix::Publisher<std::int32_t, pythonChannelCapacity>> publisher;

    Int32PublisherHandle(const char* nodeName, const char* topicName)
        : node(nodeName), publisher(node.createPublisher<std::int32_t, pythonChannelCapacity>(topicName)) {}
};

struct Int32SubscriberHandle {
    nix::Node node;
    std::unique_ptr<nix::Subscriber<std::int32_t, pythonChannelCapacity>> subscriber;

    Int32SubscriberHandle(const char* nodeName, const char* topicName)
        : node(nodeName), subscriber(node.createSubscriber<std::int32_t, pythonChannelCapacity>(topicName)) {}
};

void setLastError(const std::exception& error) {
    lastError = error.what();
}

} // namespace

extern "C" {

const char* nix_python_last_error() {
    return lastError.c_str();
}

void* nix_python_int32_publisher_create(const char* nodeName, const char* topicName) {
    try {
        lastError.clear();
        return new Int32PublisherHandle(nodeName, topicName);
    } catch (const std::exception& error) {
        setLastError(error);
        return nullptr;
    }
}

int nix_python_int32_publish(void* rawHandle, std::int32_t value) {
    try {
        if (rawHandle == nullptr) {
            lastError = "publisher handle is null";
            return -1;
        }
        lastError.clear();
        auto& handle = *static_cast<Int32PublisherHandle*>(rawHandle);
        return handle.publisher->publish(value) ? 1 : 0;
    } catch (const std::exception& error) {
        setLastError(error);
        return -1;
    }
}

void nix_python_int32_publisher_destroy(void* rawHandle) {
    delete static_cast<Int32PublisherHandle*>(rawHandle);
}

void* nix_python_int32_subscriber_create(const char* nodeName, const char* topicName) {
    try {
        lastError.clear();
        return new Int32SubscriberHandle(nodeName, topicName);
    } catch (const std::exception& error) {
        setLastError(error);
        return nullptr;
    }
}

int nix_python_int32_receive(void* rawHandle, std::int32_t* value) {
    try {
        if (rawHandle == nullptr || value == nullptr) {
            lastError = "subscriber handle or output pointer is null";
            return -1;
        }
        lastError.clear();
        auto& handle = *static_cast<Int32SubscriberHandle*>(rawHandle);
        const auto received = handle.subscriber->receiveBlocking();
        if (!received.has_value()) {
            return 0;
        }
        *value = *received;
        return 1;
    } catch (const std::exception& error) {
        setLastError(error);
        return -1;
    }
}

void nix_python_int32_subscriber_destroy(void* rawHandle) {
    delete static_cast<Int32SubscriberHandle*>(rawHandle);
}

} // extern "C"