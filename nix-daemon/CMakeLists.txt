add_executable(nix-daemon src/daemon.cxx)
target_link_libraries(nix-daemon PRIVATE nix::core Threads::Threads)
apply_nix_warnings(nix-daemon)

add_executable(nix-cleanup-shm src/cleanup_shm.cxx)
target_link_libraries(nix-cleanup-shm PRIVATE nix::core)
apply_nix_warnings(nix-cleanup-shm)

install(TARGETS nix-daemon nix-cleanup-shm
    EXPORT nixTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)