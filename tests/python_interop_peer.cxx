#include <nix/nix.hxx>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string_view>
#include <thread>
#include <typeinfo>

namespace {

constexpr std::uint64_t capacity = 16;
constexpr std::string_view schema = "nix.counter.int32/v1";
constexpr std::string_view encoding = "application/x-nix-int32-le";
bool hasEndpoint(nix::Node& node, std::string_view topic, nix::EndpointRole role) {
    for (const auto& endpoint : node.discover(topic)) {
        if (endpoint.role == role && endpoint.typeName == typeid(nix::SerializedMessage).name()) {
            return true;
        }
    }
    return false;
}

bool waitForEndpoint(nix::Node& node, std::string_view topic, nix::EndpointRole role) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (hasEndpoint(node, topic, role)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

nix::SerializedMessage messageFor(std::int32_t value) {
    const auto bits = static_cast<std::uint32_t>(value);
    const std::array<std::uint8_t, 4> payload{
        static_cast<std::uint8_t>(bits & 0xffU),
        static_cast<std::uint8_t>((bits >> 8U) & 0xffU),
        static_cast<std::uint8_t>((bits >> 16U) & 0xffU),
        static_cast<std::uint8_t>((bits >> 24U) & 0xffU),
    };
    return nix::makeSerializedMessage(schema, encoding, payload.data(), payload.size());
}

bool receiveExpected(nix::Subscriber<nix::SerializedMessage, capacity>& subscriber, std::int32_t expected) {
    const auto received = subscriber.receiveBlocking();
    if (!received.has_value()) {
        return false;
    }
    const auto decoded = nix::decodeSerializedMessage(*received);
    if (decoded.schema != schema || decoded.encoding != encoding || decoded.payload.size() != 4U) {
        return false;
    }
    const std::uint32_t bits = static_cast<std::uint32_t>(decoded.payload[0])
        | (static_cast<std::uint32_t>(decoded.payload[1]) << 8U)
        | (static_cast<std::uint32_t>(decoded.payload[2]) << 16U)
        | (static_cast<std::uint32_t>(decoded.payload[3]) << 24U);
    return static_cast<std::int32_t>(bits) == expected;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "Usage: python_interop_peer <cpp-publish|cpp-subscribe> <topic>\n";
        return 2;
    }

    nix::Node node("python_interop_cpp_peer");
    const std::string_view mode(argv[1]);
    const std::string_view topic(argv[2]);
    if (mode == "cpp-publish") {
        auto publisher = node.createPublisher<nix::SerializedMessage, capacity>(topic);
        if (!waitForEndpoint(node, topic, nix::EndpointRole::Subscriber)) {
            std::cerr << "Python subscriber did not register\n";
            return 1;
        }
        for (std::int32_t value = 41; value < 44; ++value) {
            if (!publisher->publish(messageFor(value))) {
                std::cerr << "C++ publish failed\n";
                return 1;
            }
        }
        return 0;
    }
    if (mode == "cpp-subscribe") {
        auto subscriber = node.createSubscriber<nix::SerializedMessage, capacity>(topic);
        for (std::int32_t value = 51; value < 54; ++value) {
            if (!receiveExpected(*subscriber, value)) {
                std::cerr << "C++ received an unexpected Python message\n";
                return 1;
            }
        }
        return 0;
    }

    std::cerr << "Unknown peer mode: " << mode << '\n';
    return 2;
}