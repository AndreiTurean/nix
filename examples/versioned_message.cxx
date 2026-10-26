#include <nix/shm_channel.hxx>

#include <cstdint>
#include <iostream>
#include <string_view>

struct TelemetryV1 {
    std::uint32_t sequence;
    std::int32_t temperatureMilliCelsius;
};

template <>
struct nix::SharedTypeFingerprint<TelemetryV1> {
    // Change this value whenever field order, meaning, or wire representation changes.
    static constexpr std::uint64_t value = 0x54454c454d455401ULL;
};

int main() {
    constexpr std::string_view channelName = "/nix_example_telemetry_v1";
    constexpr std::uint64_t capacity = 8;

    nix::ShmChannel<TelemetryV1, capacity> channel(channelName, nix::ShmMode::Create);
    const TelemetryV1 outgoing{1U, 21500};
    if (!channel.push(outgoing)) {
        std::cerr << "channel is full\n";
        return 1;
    }

    const auto incoming = channel.pop();
    if (!incoming.has_value()) {
        std::cerr << "channel is empty\n";
        return 1;
    }
    std::cout << "sequence=" << incoming->sequence
        << " temperature_mC=" << incoming->temperatureMilliCelsius << '\n';
    return 0;
}