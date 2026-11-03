add_executable(nix-list src/nix_list.cxx)
target_link_libraries(nix-list PRIVATE nix::core)
apply_nix_warnings(nix-list)

add_executable(nix-latency src/nix_latency.cxx)
target_link_libraries(nix-latency PRIVATE nix::core Threads::Threads)
apply_nix_warnings(nix-latency)

install(TARGETS nix-list nix-latency
    EXPORT nixTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)