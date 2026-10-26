#pragma once

#include <nix/shm_channel.hxx>
#include <string_view>

namespace nix {

template <typename T, std::uint64_t Capacity>
class Publisher {
public:
    explicit Publisher(std::string_view topicName)
        : _channel(topicName, ShmMode::Create) {}

    bool publish(const T& message) {
        return _channel.push(message);
    }

    std::uint64_t capacity() const {
        return _channel.capacity();
    }

private:
    ShmChannel<T, Capacity> _channel;
};

} // namespace nix
