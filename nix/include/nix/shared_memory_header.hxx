#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>

namespace nix {

inline constexpr std::uint64_t sharedMemoryMagic = 0x4e495853484d3031ULL;
inline constexpr std::uint32_t sharedMemoryVersion = 4U;
inline constexpr std::uint32_t sharedMemoryInitializing = 0U;
inline constexpr std::uint32_t sharedMemoryReady = 1U;

struct SharedMemoryHeader {
    std::uint64_t magic;
    std::uint32_t version;
    std::uint32_t headerSize;
    std::uint64_t ownerPid;
    std::uint64_t ownerStartTime;
    std::int64_t createdAtSeconds;
    std::uint64_t mappedSize;
    std::uint64_t capacity;
    std::uint64_t elementSize;
    std::uint64_t typeFingerprint;
    std::atomic<std::uint32_t> initializationState;
};

namespace detail {

consteval std::uint64_t fingerprintBytes(std::string_view text, std::uint64_t hash = 14695981039346656037ULL) {
    for (const char value : text) {
        hash ^= static_cast<unsigned char>(value);
        hash *= 1099511628211ULL;
    }
    return hash;
}

template <typename T>
consteval std::uint64_t defaultTypeFingerprint() {
#if defined(__clang__) || defined(__GNUC__)
    constexpr std::string_view typeSignature = __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
    constexpr std::string_view typeSignature = __FUNCSIG__;
#else
#error "Define nix::SharedTypeFingerprint<T> for this compiler"
#endif
    std::uint64_t hash = fingerprintBytes(typeSignature);
    hash ^= sizeof(T);
    hash *= 1099511628211ULL;
    hash ^= alignof(T);
    return hash * 1099511628211ULL;
}

} // namespace detail

template <typename T>
struct SharedTypeFingerprint {
    static constexpr std::uint64_t value = detail::defaultTypeFingerprint<T>();
};

} // namespace nix