#pragma once

#include <nix/core.hxx>
#include <string>
#include <string_view>
#include <system_error>
#include <memory>
#include <new>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace nix {

enum class ShmMode {
    Create, // Creates the shared memory segment, truncates it, and constructs the ring buffer
    Open    // Opens an existing shared memory segment and maps to it
};

template <typename T, std::uint64_t Capacity>
class ShmChannel {
public:
    ShmChannel(std::string_view name, ShmMode mode)
        : _name(name), _isCreator(mode == ShmMode::Create), _fd(-1) {

        int flags = O_RDWR;
        if (_isCreator) {
            flags |= O_CREAT | O_EXCL;
        }

        std::string shmPath = (_name.empty() || _name[0] != '/') ? std::string("/") + std::string(_name) : std::string(_name);

        constexpr mode_t permissions = 0666;
        _fd = shm_open(shmPath.c_str(), flags, permissions);
        if (_fd == -1) {
            throw std::system_error(errno, std::generic_category(), "shm_open failed");
        }

        const std::size_t totalSize = sizeof(SpscRingBuffer<T, Capacity>);

        if (_isCreator) {
            if (ftruncate(_fd, static_cast<off_t>(totalSize)) == -1) {
                int savedErrno = errno;
                shm_unlink(shmPath.c_str());
                close(_fd);
                throw std::system_error(savedErrno, std::generic_category(), "ftruncate failed");
            }
        }

        void* rawPtr = mmap(nullptr, totalSize, PROT_READ | PROT_WRITE, MAP_SHARED, _fd, 0);
        if (rawPtr == MAP_FAILED) {
            int savedErrno = errno;
            if (_isCreator) {
                shm_unlink(shmPath.c_str());
            }
            close(_fd);
            throw std::system_error(savedErrno, std::generic_category(), "mmap failed");
        }

        // Wrap the raw mmap pointer in a smart unique_ptr with a custom lambda deleter for munmap
        _mappedMemory = std::unique_ptr<void, MmapDeleter>(rawPtr, MmapDeleter{totalSize});

        if (_isCreator) {
            // Placement-new wrapped cleanly under the hood so client code doesn't see it
            _ringBuffer = ::new (_mappedMemory.get()) SpscRingBuffer<T, Capacity>();
        } else {
            _ringBuffer = static_cast<SpscRingBuffer<T, Capacity>*>(_mappedMemory.get());
        }
    }

    ~ShmChannel() {
        // Smart pointers and RAII handle munmap, close, and shm_unlink automatically
        if (_isCreator && !_name.empty()) {
            std::string shmPath = (_name[0] == '/') ? std::string(_name) : "/" + std::string(_name);
            shm_unlink(shmPath.c_str());
        }
        if (_fd != -1) {
            close(_fd);
        }
    }

    // Rule of Zero/Five: explicitly delete copy, default move since unique_ptr handles resource tracking
    ShmChannel(const ShmChannel&) = delete;
    ShmChannel& operator=(const ShmChannel&) = delete;
    ShmChannel(ShmChannel&&) noexcept = default;
    ShmChannel& operator=(ShmChannel&&) noexcept = default;

    // Ring buffer API delegates
    bool push(const T& item) {
        return _ringBuffer->push(item);
    }

    std::optional<T> pop() {
        return _ringBuffer->pop();
    }

    bool empty() const {
        return _ringBuffer->empty();
    }

    std::uint64_t size() const {
        return _ringBuffer->size();
    }

    constexpr std::uint64_t capacity() const {
        return Capacity;
    }

private:
    struct MmapDeleter {
        std::size_t size;
        void operator()(void* ptr) const {
            if (ptr && ptr != MAP_FAILED) {
                munmap(ptr, size);
            }
        }
    };

    std::string _name;
    bool _isCreator;
    int _fd;
    std::unique_ptr<void, MmapDeleter> _mappedMemory;
    SpscRingBuffer<T, Capacity>* _ringBuffer;
};

} // namespace nix
