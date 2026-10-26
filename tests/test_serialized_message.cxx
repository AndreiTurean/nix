#include <nix/serialized_message.hxx>

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string_view>

int main() {
    constexpr std::array<std::uint8_t, 25> expectedPrefix{
        'N', 'I', 'X', 'M', 0x00, 0x01, 0x00, 0x04, 0x00, 0x03, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x02, 's', '/', 'v', '1', 'r', 'a', 'w', 0x01, 0x02,
    };
    constexpr std::array<std::uint8_t, 2> payload{0x01, 0x02};
    const auto message = nix::makeSerializedMessage("s/v1", "raw", payload.data(), payload.size());
    assert(std::equal(expectedPrefix.begin(), expectedPrefix.end(), message.bytes.begin()));
    assert(std::all_of(message.bytes.begin() + static_cast<std::ptrdiff_t>(expectedPrefix.size()),
        message.bytes.end(), [](std::uint8_t value) { return value == 0U; }));

    const auto decoded = nix::decodeSerializedMessage(message);
    assert(decoded.version == nix::serializedMessageVersion);
    assert(decoded.schema == "s/v1");
    assert(decoded.encoding == "raw");
    assert(decoded.payload.size() == payload.size());
    assert(std::equal(decoded.payload.begin(), decoded.payload.end(), payload.begin()));

    const auto assertInvalid = [](nix::SerializedMessage malformed) {
        bool rejected = false;
        try {
            static_cast<void>(nix::decodeSerializedMessage(malformed));
        } catch (const std::invalid_argument&) {
            rejected = true;
        }
        assert(rejected);
    };

    auto malformed = message;
    malformed.bytes[0] = 'X';
    assertInvalid(malformed);

    malformed = message;
    malformed.bytes[5] = 0x02;
    assertInvalid(malformed);

    malformed = message;
    malformed.bytes[10] = 0x01;
    assertInvalid(malformed);

    malformed = message;
    malformed.bytes[7] = 0xff;
    assertInvalid(malformed);

    malformed = message;
    malformed.bytes[9] = 0x00;
    assertInvalid(malformed);

    malformed = message;
    malformed.bytes[14] = 0x02;
    malformed.bytes[15] = 0x01;
    assertInvalid(malformed);

    malformed = message;
    malformed.bytes[nix::serializedMessageHeaderSize + 1U] = 0x00;
    assertInvalid(malformed);

    malformed = message;
    malformed.bytes.back() = 0x01;
    assertInvalid(malformed);

    bool rejectedOversized = false;
    try {
        static_cast<void>(nix::makeSerializedMessage("schema", "raw", payload.data(),
            nix::serializedPayloadCapacity + 1U));
    } catch (const std::invalid_argument&) {
        rejectedOversized = true;
    }
    assert(rejectedOversized);

    return 0;
}