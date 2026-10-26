#pragma once

#include <nix/shm_channel.hxx>
#include <string_view>
#include <optional>

namespace nix {

template <typename T, std::uint64_t Capacity>
class Subscriber {
public:
    explicit Subscriber(std::string_view topicName)
        : _channel(topicName, ShmMode::Open) {}

    std::optional<T> receive() {
        return _channel.pop();
    }

    bool empty() const {
        return _channel.empty();
    }

    std::uint64_t size() const {
        return _channel.size();
    }

private:
    ShmChannel<T, Capacity> _channel;
};

} // namespace nix
