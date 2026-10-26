#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>

namespace nix {

inline constexpr std::uint16_t serializedMessageVersion = 1U;
inline constexpr std::size_t serializedSchemaCapacity = 64U;
inline constexpr std::size_t serializedEncodingCapacity = 32U;
inline constexpr std::size_t serializedPayloadCapacity = 512U;

struct SerializedMessage {
    std::uint16_t version;
    std::uint16_t schemaLength;
    std::uint16_t encodingLength;
    std::uint16_t reserved;
    std::uint32_t payloadLength;
    std::array<char, serializedSchemaCapacity> schema;
    std::array<char, serializedEncodingCapacity> encoding;
    std::array<std::uint8_t, serializedPayloadCapacity> payload;
};

inline SerializedMessage makeSerializedMessage(std::string_view schema, std::string_view encoding,
    const std::uint8_t* payload, std::size_t payloadLength) {
    if (schema.empty() || schema.size() > serializedSchemaCapacity
        || encoding.empty() || encoding.size() > serializedEncodingCapacity
        || payloadLength > serializedPayloadCapacity
        || (payloadLength != 0U && payload == nullptr)) {
        throw std::invalid_argument("serialized message metadata or payload exceeds its capacity");
    }

    SerializedMessage message{};
    message.version = serializedMessageVersion;
    message.schemaLength = static_cast<std::uint16_t>(schema.size());
    message.encodingLength = static_cast<std::uint16_t>(encoding.size());
    message.payloadLength = static_cast<std::uint32_t>(payloadLength);
    std::memcpy(message.schema.data(), schema.data(), schema.size());
    std::memcpy(message.encoding.data(), encoding.data(), encoding.size());
    if (payloadLength != 0U) {
        std::memcpy(message.payload.data(), payload, payloadLength);
    }
    return message;
}

} // namespace nix