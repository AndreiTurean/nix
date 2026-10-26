#include <nix/shared_memory_header.hxx>

#include <cerrno>
#include <signal.h>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace {

std::uint64_t processStartTime(std::uint64_t processId) {
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

bool processIsAlive(const nix::SharedMemoryHeader& header) {
    if (header.ownerPid == 0 || header.ownerPid > static_cast<std::uint64_t>(INT32_MAX)) {
        return false;
    }
    const pid_t ownerPid = static_cast<pid_t>(header.ownerPid);
    if (kill(ownerPid, 0) == -1 && errno == ESRCH) {
        return false;
    }
    const std::uint64_t currentStartTime = processStartTime(header.ownerPid);
    if (currentStartTime == 0 || header.ownerStartTime == 0) {
        return true;
    }
    return currentStartTime == header.ownerStartTime;
}

bool isNixChannel(const struct stat& fileStatus, const nix::SharedMemoryHeader& header) {
    return fileStatus.st_size >= static_cast<off_t>(sizeof(nix::SharedMemoryHeader))
        && header.magic == nix::sharedMemoryMagic
        && header.version == nix::sharedMemoryVersion
        && header.headerSize == sizeof(nix::SharedMemoryHeader)
        && header.initializationState.load(std::memory_order_acquire) == nix::sharedMemoryReady
        && header.mappedSize == static_cast<std::uint64_t>(fileStatus.st_size);
}

} // namespace

int main(int argc, char** argv) {
    const bool apply = argc == 2 && std::string_view(argv[1]) == "--apply";
    if (argc > 2 || (argc == 2 && !apply)) {
        std::cerr << "Usage: nix-cleanup-shm [--apply]\n";
        return 2;
    }

    std::size_t staleCount = 0;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/shm")) {
        const std::string objectName = entry.path().filename().string();
        if (objectName.empty() || objectName[0] == '.') {
            continue;
        }
        const int descriptor = shm_open(("/" + objectName).c_str(), O_RDWR, 0);
        if (descriptor == -1) {
            continue;
        }
        struct stat fileStatus{};
        if (fstat(descriptor, &fileStatus) == -1
            || fileStatus.st_size < static_cast<off_t>(sizeof(nix::SharedMemoryHeader))) {
            close(descriptor);
            continue;
        }
        void* mapped = mmap(nullptr, sizeof(nix::SharedMemoryHeader), PROT_READ, MAP_SHARED, descriptor, 0);
        if (mapped == MAP_FAILED) {
            close(descriptor);
            continue;
        }

        const auto& header = *static_cast<const nix::SharedMemoryHeader*>(mapped);
        if (isNixChannel(fileStatus, header) && !processIsAlive(header)) {
            ++staleCount;
            std::cout << (apply ? "unlinking " : "stale ") << "/" << objectName
                << " (owner PID " << header.ownerPid << ")\n";
            if (apply && shm_unlink(("/" + objectName).c_str()) == -1 && errno != ENOENT) {
                std::cerr << "failed to unlink /" << objectName << ": " << errno << '\n';
            }
        }

        munmap(mapped, sizeof(nix::SharedMemoryHeader));
        close(descriptor);
    }

    if (!apply) {
        std::cout << staleCount << " stale Nix channel(s) found; pass --apply to unlink them\n";
    } else {
        std::cout << staleCount << " stale Nix channel(s) removed\n";
    }
    return 0;
}