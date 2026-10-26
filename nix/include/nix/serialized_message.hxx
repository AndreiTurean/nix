#pragma once

#include <nix/shared_memory_header.hxx>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace nix {

inline constexpr std::uint16_t serializedMessageVersion = 1U;
inline constexpr std::size_t serializedSchemaCapacity = 64U;
inline constexpr std::size_t serializedEncodingCapacity = 32U;
inline constexpr std::size_t serializedPayloadCapacity = std::size_t{63U} * 1024U;
inline constexpr std::size_t serializedMessageHeaderSize = 16U;
inline constexpr std::size_t serializedMessageCapacity = serializedMessageHeaderSize
    + serializedSchemaCapacity + serializedEncodingCapacity + serializedPayloadCapacity;
inline constexpr std::array<std::uint8_t, 4> serializedMessageMagic{'N', 'I', 'X', 'M'};

struct SerializedMessage {
    std::array<std::uint8_t, serializedMessageCapacity> bytes{};
};

static_assert(sizeof(SerializedMessage) == serializedMessageCapacity);
static_assert(std::is_trivially_copyable_v<SerializedMessage>);

struct SerializedMessageView {
    std::uint16_t version;
    std::string_view schema;
    std::string_view encoding;
    std::span<const std::uint8_t> payload;
};

namespace detail {

inline void writeBigEndian16(std::array<std::uint8_t, serializedMessageCapacity>& bytes,
    std::size_t offset, std::uint16_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
    bytes[offset + 1U] = static_cast<std::uint8_t>(value & 0xffU);
}

inline void writeBigEndian32(std::array<std::uint8_t, serializedMessageCapacity>& bytes,
    std::size_t offset, std::uint32_t value) {
    bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
    bytes[offset + 1U] = static_cast<std::uint8_t>((value >> 16U) & 0xffU);
    bytes[offset + 2U] = static_cast<std::uint8_t>((value >> 8U) & 0xffU);
    bytes[offset + 3U] = static_cast<std::uint8_t>(value & 0xffU);
}

inline std::uint16_t readBigEndian16(const std::array<std::uint8_t, serializedMessageCapacity>& bytes,
    std::size_t offset) {
    const std::uint32_t high = bytes[offset];
    const std::uint32_t low = bytes[offset + 1U];
    return static_cast<std::uint16_t>((high << 8U) | low);
}

inline std::uint32_t readBigEndian32(const std::array<std::uint8_t, serializedMessageCapacity>& bytes,
    std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24U)
        | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U)
        | (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U)
        | static_cast<std::uint32_t>(bytes[offset + 3U]);
}

inline bool validLabel(std::string_view label) {
    return !label.empty() && std::all_of(label.begin(), label.end(), [](char value) {
        return value >= 0x20 && value <= 0x7e;
    });
}

} // namespace detail

inline SerializedMessage makeSerializedMessage(std::string_view schema, std::string_view encoding,
    const std::uint8_t* payload, std::size_t payloadLength) {
    if (!detail::validLabel(schema) || schema.size() > serializedSchemaCapacity
        || !detail::validLabel(encoding) || encoding.size() > serializedEncodingCapacity
        || payloadLength > serializedPayloadCapacity
        || (payloadLength != 0U && payload == nullptr)) {
        throw std::invalid_argument("serialized message metadata or payload exceeds its capacity or format");
    }

    SerializedMessage message{};
    std::copy(serializedMessageMagic.begin(), serializedMessageMagic.end(), message.bytes.begin());
    detail::writeBigEndian16(message.bytes, 4U, serializedMessageVersion);
    detail::writeBigEndian16(message.bytes, 6U, static_cast<std::uint16_t>(schema.size()));
    detail::writeBigEndian16(message.bytes, 8U, static_cast<std::uint16_t>(encoding.size()));
    detail::writeBigEndian16(message.bytes, 10U, 0U);
    detail::writeBigEndian32(message.bytes, 12U, static_cast<std::uint32_t>(payloadLength));

    std::size_t offset = serializedMessageHeaderSize;
    std::copy(schema.begin(), schema.end(), message.bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += schema.size();
    std::copy(encoding.begin(), encoding.end(), message.bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += encoding.size();
    if (payloadLength != 0U) {
        std::copy_n(payload, payloadLength, message.bytes.begin() + static_cast<std::ptrdiff_t>(offset));
    }
    return message;
}

inline SerializedMessageView decodeSerializedMessage(const SerializedMessage& message) {
    const auto& bytes = message.bytes;
    if (!std::equal(serializedMessageMagic.begin(), serializedMessageMagic.end(), bytes.begin())) {
        throw std::invalid_argument("serialized message magic does not match");
    }

    const std::uint16_t version = detail::readBigEndian16(bytes, 4U);
    const std::uint16_t schemaLength = detail::readBigEndian16(bytes, 6U);
    const std::uint16_t encodingLength = detail::readBigEndian16(bytes, 8U);
    const std::uint16_t reserved = detail::readBigEndian16(bytes, 10U);
    const std::uint32_t payloadLength = detail::readBigEndian32(bytes, 12U);
    if (version != serializedMessageVersion || reserved != 0U
        || schemaLength == 0U || schemaLength > serializedSchemaCapacity
        || encodingLength == 0U || encodingLength > serializedEncodingCapacity
        || payloadLength > serializedPayloadCapacity) {
        throw std::invalid_argument("serialized message header fields are invalid");
    }

    const std::size_t schemaOffset = serializedMessageHeaderSize;
    const std::size_t encodingOffset = schemaOffset + schemaLength;
    const std::size_t payloadOffset = encodingOffset + encodingLength;
    const std::size_t frameLength = payloadOffset + payloadLength;
    if (frameLength > bytes.size()) {
        throw std::invalid_argument("serialized message lengths exceed frame capacity");
    }

    const std::string_view schema(reinterpret_cast<const char*>(bytes.data() + schemaOffset), schemaLength);
    const std::string_view encoding(reinterpret_cast<const char*>(bytes.data() + encodingOffset), encodingLength);
    if (!detail::validLabel(schema) || !detail::validLabel(encoding)) {
        throw std::invalid_argument("serialized message labels are invalid");
    }
    if (!std::all_of(bytes.begin() + static_cast<std::ptrdiff_t>(frameLength), bytes.end(),
            [](std::uint8_t value) { return value == 0U; })) {
        throw std::invalid_argument("serialized message padding must be zero");
    }

    return SerializedMessageView{
        version,
        schema,
        encoding,
        std::span<const std::uint8_t>(bytes.data() + payloadOffset, payloadLength),
    };
}

template <>
struct SharedTypeFingerprint<SerializedMessage> {
    static constexpr std::uint64_t value = 0x4e49584d00010002ULL;
};

} // namespace nix
