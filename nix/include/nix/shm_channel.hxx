#pragma once

#include <nix/spsc_ring_buffer.hxx>
#include <nix/shared_memory_header.hxx>
#include <cerrno>
#include <ctime>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <memory>
#include <pthread.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace nix {

enum class ShmMode : uint8_t {
    Create = 0x00, // Creates the shared memory segment, truncates it, and constructs the ring buffer
    Open = 0x01,    // Opens an existing shared memory segment and maps to it
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
        if (_fd == -1 && _isCreator && errno == EEXIST && unlinkIfOwnerIsDead(shmPath)) {
            _fd = shm_open(shmPath.c_str(), flags, permissions);
        }
        if (_fd == -1) {
            const int savedErrno = errno;
            const std::string message = _isCreator && savedErrno == EEXIST
                ? "shared-memory channel already exists and its owner is alive or its metadata is invalid"
                : "shm_open failed";
            throw std::system_error(savedErrno, std::generic_category(), message);
        }

        const std::uint64_t totalSize = sizeof(SharedState);

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
            _sharedState = ::new (_mappedMemory.get()) SharedState;
            _sharedState->subscriberCount = 0;
            _sharedState->header.initializationState.store(sharedMemoryInitializing, std::memory_order_relaxed);
            _sharedState->header.magic = sharedMemoryMagic;
            _sharedState->header.version = sharedMemoryVersion;
            _sharedState->header.headerSize = static_cast<std::uint32_t>(sizeof(SharedMemoryHeader));
            _sharedState->header.ownerPid = static_cast<std::uint64_t>(getpid());
            _sharedState->header.ownerStartTime = processStartTime(getpid());
            _sharedState->header.createdAtSeconds = std::time(nullptr);
            _sharedState->header.mappedSize = totalSize;
            _sharedState->header.capacity = Capacity;
            _sharedState->header.elementSize = sizeof(T);
            _sharedState->header.typeFingerprint = SharedTypeFingerprint<T>::value;
            const int initError = initializeSynchronization(*_sharedState);
            if (initError != 0) {
                std::string unlinkPath = (_name.empty() || _name[0] != '/') ? "/" + _name : _name;
                shm_unlink(unlinkPath.c_str());
                close(_fd);
                _fd = -1;
                throw std::system_error(initError, std::generic_category(), "shared synchronization initialization failed");
            }
            _sharedState->header.initializationState.store(sharedMemoryReady, std::memory_order_release);
        } else {
            _sharedState = static_cast<SharedState*>(_mappedMemory.get());
            if (_sharedState->header.magic != sharedMemoryMagic
                || _sharedState->header.version != sharedMemoryVersion
                || _sharedState->header.initializationState.load(std::memory_order_acquire) != sharedMemoryReady
                || _sharedState->header.capacity != Capacity
                || _sharedState->header.elementSize != sizeof(T)
                || _sharedState->header.typeFingerprint != SharedTypeFingerprint<T>::value) {
                close(_fd);
                _fd = -1;
                throw std::runtime_error("shared-memory channel metadata or message type fingerprint mismatch");
            }
        }
    }

    ~ShmChannel() {
        if (_isCreator && !_name.empty()) {
            std::string shmPath = (_name[0] == '/') ? std::string(_name) : "/" + std::string(_name);
            shm_unlink(shmPath.c_str());
        }
        if (_fd != -1) {
            close(_fd);
        }
    }

    ShmChannel(const ShmChannel&) = delete;
    ShmChannel& operator=(const ShmChannel&) = delete;
    ShmChannel(ShmChannel&&) noexcept = default;
    ShmChannel& operator=(ShmChannel&&) noexcept = default;

    void discardPending() {
        MutexLock lock(_sharedState->mutex, _sharedState->condition);
        while (_sharedState->ringBuffer.pop().has_value()) {
        }
    }

    void addSubscriber() {
        MutexLock lock(_sharedState->mutex, _sharedState->condition);
        if (_sharedState->subscriberCount == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error("shared-memory subscriber count overflow");
        }
        ++_sharedState->subscriberCount;
    }

    void removeSubscriber() noexcept {
        try {
            MutexLock lock(_sharedState->mutex, _sharedState->condition);
            if (_sharedState->subscriberCount > 0) {
                --_sharedState->subscriberCount;
                if (_sharedState->subscriberCount == 0) {
                    while (_sharedState->ringBuffer.pop().has_value()) {
                    }
                }
            }
        } catch (const std::exception&) {
        }
    }

    bool pushIfSubscribed(const T& item) {
        MutexLock lock(_sharedState->mutex, _sharedState->condition);
        if (_sharedState->subscriberCount == 0) {
            while (_sharedState->ringBuffer.pop().has_value()) {
            }
            return true;
        }
        const bool pushed = _sharedState->ringBuffer.push(item);
        if (pushed) {
            const int signalError = pthread_cond_signal(&_sharedState->condition);
            if (signalError != 0) {
                throw std::system_error(signalError, std::generic_category(), "pthread_cond_signal failed");
            }
        }
        return pushed;
    }

    // Ring buffer API delegates
    bool push(const T& item) {
        MutexLock lock(_sharedState->mutex, _sharedState->condition);
        const bool pushed = _sharedState->ringBuffer.push(item);
        if (pushed) {
            const int signalError = pthread_cond_signal(&_sharedState->condition);
            if (signalError != 0) {
                throw std::system_error(signalError, std::generic_category(), "pthread_cond_signal failed");
            }
        }
        return pushed;
    }

    std::optional<T> pop() {
        MutexLock lock(_sharedState->mutex, _sharedState->condition);
        return _sharedState->ringBuffer.pop();
    }

    std::optional<T> waitPop() {
        MutexLock lock(_sharedState->mutex, _sharedState->condition);
        while (_sharedState->ringBuffer.empty()) {
            const int waitError = lock.wait(_sharedState->condition);
            if (waitError != 0) {
                throw std::system_error(waitError, std::generic_category(), "pthread_cond_wait failed");
            }
        }

        return _sharedState->ringBuffer.pop();
    }

    bool empty() const {
        MutexLock lock(_sharedState->mutex, _sharedState->condition);
        return _sharedState->ringBuffer.empty();
    }

    std::uint64_t size() const {
        MutexLock lock(_sharedState->mutex, _sharedState->condition);
        return _sharedState->ringBuffer.size();
    }

    constexpr std::uint64_t capacity() const {
        return Capacity;
    }

private:
    class MutexLock {
    public:
        MutexLock(pthread_mutex_t& mutex, pthread_cond_t& condition) : _mutex(mutex), _condition(condition) {
            int error = pthread_mutex_lock(&_mutex);
            if (error == EOWNERDEAD) {
                error = recover();
            }
            if (error != 0) {
                throw std::system_error(error, std::generic_category(), "pthread_mutex_lock failed");
            }
        }

        int wait(pthread_cond_t& condition) {
            int error = pthread_cond_wait(&condition, &_mutex);
            if (error == EOWNERDEAD) {
                error = recover();
            }
            return error;
        }

        ~MutexLock() {
            pthread_mutex_unlock(&_mutex);
        }

        MutexLock(const MutexLock&) = delete;
        MutexLock& operator=(const MutexLock&) = delete;

    private:
        int recover() {
            const int consistentError = pthread_mutex_consistent(&_mutex);
            if (consistentError != 0) {
                return consistentError;
            }
            return pthread_cond_broadcast(&_condition);
        }

        pthread_mutex_t& _mutex;
        pthread_cond_t& _condition;
    };

    struct SharedState {
        SharedMemoryHeader header;
        SpscRingBuffer<T, Capacity> ringBuffer;
        std::uint64_t subscriberCount;
        pthread_mutex_t mutex;
        pthread_cond_t condition;
    };

    static std::uint64_t processStartTime(pid_t processId) {
        std::ifstream statFile("/proc/" + std::to_string(processId) + "/stat");
        std::string statLine;
        if (!std::getline(statFile, statLine)) {
            return 0;
        }
        const std::size_t commandEnd = statLine.rfind(')');
        if (commandEnd == std::string::npos || commandEnd + 2U >= statLine.size()) {
            return 0;
        }
        std::istringstream fields(statLine.substr(commandEnd + 2U));
        std::string field;
        for (int index = 0; index <= 19; ++index) {
            if (!(fields >> field)) {
                return 0;
            }
        }
        try {
            return std::stoull(field);
        } catch (const std::exception&) {
            return 0;
        }
    }

    static bool unlinkIfOwnerIsDead(const std::string& shmPath) {
        const int existingFd = shm_open(shmPath.c_str(), O_RDONLY, 0);
        if (existingFd == -1) {
            return false;
        }

        struct stat fileStatus{};
        if (fstat(existingFd, &fileStatus) == -1
            || fileStatus.st_size < static_cast<off_t>(sizeof(SharedMemoryHeader))) {
            close(existingFd);
            return false;
        }
        void* mapped = mmap(nullptr, sizeof(SharedMemoryHeader), PROT_READ, MAP_SHARED, existingFd, 0);
        close(existingFd);
        if (mapped == MAP_FAILED) {
            return false;
        }

        const auto& header = *static_cast<const SharedMemoryHeader*>(mapped);
        const bool validHeader = header.magic == sharedMemoryMagic
            && header.version == sharedMemoryVersion
            && header.headerSize == sizeof(SharedMemoryHeader)
            && header.initializationState.load(std::memory_order_acquire) == sharedMemoryReady
            && header.mappedSize == static_cast<std::uint64_t>(fileStatus.st_size);
        bool ownerIsAlive = true;
        if (validHeader && header.ownerPid != 0
            && header.ownerPid <= static_cast<std::uint64_t>(std::numeric_limits<pid_t>::max())) {
            const pid_t ownerPid = static_cast<pid_t>(header.ownerPid);
            if (kill(ownerPid, 0) == -1 && errno == ESRCH) {
                ownerIsAlive = false;
            } else {
                const std::uint64_t currentStartTime = processStartTime(ownerPid);
                if (currentStartTime != 0 && header.ownerStartTime != 0
                    && currentStartTime != header.ownerStartTime) {
                    ownerIsAlive = false;
                }
            }
        }
        munmap(mapped, sizeof(SharedMemoryHeader));

        if (!validHeader || ownerIsAlive) {
            return false;
        }
        return shm_unlink(shmPath.c_str()) == 0 || errno == ENOENT;
    }

    static int initializeSynchronization(SharedState& state) {
        pthread_mutexattr_t mutexAttributes;
        int error = pthread_mutexattr_init(&mutexAttributes);
        if (error != 0) {
            return error;
        }

        error = pthread_mutexattr_setpshared(&mutexAttributes, PTHREAD_PROCESS_SHARED);
        if (error == 0) {
            error = pthread_mutexattr_setrobust(&mutexAttributes, PTHREAD_MUTEX_ROBUST);
        }
        if (error == 0) {
            error = pthread_mutex_init(&state.mutex, &mutexAttributes);
        }
        pthread_mutexattr_destroy(&mutexAttributes);
        if (error != 0) {
            return error;
        }

        pthread_condattr_t conditionAttributes;
        error = pthread_condattr_init(&conditionAttributes);
        if (error != 0) {
            pthread_mutex_destroy(&state.mutex);
            return error;
        }

        error = pthread_condattr_setpshared(&conditionAttributes, PTHREAD_PROCESS_SHARED);
        if (error == 0) {
            error = pthread_cond_init(&state.condition, &conditionAttributes);
        }
        pthread_condattr_destroy(&conditionAttributes);
        if (error != 0) {
            pthread_mutex_destroy(&state.mutex);
        }
        return error;
    }

    struct MmapDeleter {
        std::uint64_t size;
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
    SharedState* _sharedState;
};

} // namespace nix
