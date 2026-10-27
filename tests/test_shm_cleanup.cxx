#include <nix/shm_channel.hxx>

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <string>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

int main(int argc, char** argv) {
    assert(argc == 2);
    const std::string channelName = "/nix_stale_cleanup_test_" + std::to_string(static_cast<long long>(getpid()));
    const std::string unrelatedName = channelName + "_unmarked";
    const std::string liveName = channelName + "_live";
    shm_unlink(channelName.c_str());
    shm_unlink(unrelatedName.c_str());
    shm_unlink(liveName.c_str());

    const int unrelatedDescriptor = shm_open(unrelatedName.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    assert(unrelatedDescriptor != -1);
    close(unrelatedDescriptor);

    const pid_t owner = fork();
    assert(owner != -1);
    if (owner == 0) {
        nix::ShmChannel<std::uint64_t, 8> channel(channelName, nix::ShmMode::Create);
        _exit(0);
    }

    int ownerStatus = 0;
    assert(waitpid(owner, &ownerStatus, 0) == owner);
    assert(WIFEXITED(ownerStatus) && WEXITSTATUS(ownerStatus) == 0);

    const int legacyDescriptor = shm_open(channelName.c_str(), O_RDWR, 0);
    assert(legacyDescriptor != -1);
    void* legacyMapping = mmap(nullptr, sizeof(nix::SharedMemoryHeader), PROT_READ | PROT_WRITE,
        MAP_SHARED, legacyDescriptor, 0);
    assert(legacyMapping != MAP_FAILED);
    auto& legacyHeader = *static_cast<nix::SharedMemoryHeader*>(legacyMapping);
    legacyHeader.version = nix::sharedMemoryVersion - 1U;
    assert(munmap(legacyMapping, sizeof(nix::SharedMemoryHeader)) == 0);
    close(legacyDescriptor);

    nix::ShmChannel<std::uint64_t, 8> liveChannel(liveName, nix::ShmMode::Create);

    const pid_t cleanup = fork();
    assert(cleanup != -1);
    if (cleanup == 0) {
        execl(argv[1], argv[1], "--apply", static_cast<char*>(nullptr));
        _exit(127);
    }
    int cleanupStatus = 0;
    assert(waitpid(cleanup, &cleanupStatus, 0) == cleanup);
    assert(WIFEXITED(cleanupStatus) && WEXITSTATUS(cleanupStatus) == 0);

    errno = 0;
    const int descriptor = shm_open(channelName.c_str(), O_RDWR, 0);
    assert(descriptor == -1 && errno == ENOENT);

    const int preservedDescriptor = shm_open(unrelatedName.c_str(), O_RDWR, 0);
    assert(preservedDescriptor != -1);
    close(preservedDescriptor);
    assert(shm_unlink(unrelatedName.c_str()) == 0);

    const int liveDescriptor = shm_open(liveName.c_str(), O_RDWR, 0);
    assert(liveDescriptor != -1);
    close(liveDescriptor);

    return 0;
}