add_library(nix_core
    src/nix.cxx
    src/daemon_client.cxx
)
set_target_properties(nix_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(nix::core ALIAS nix_core)

target_include_directories(nix_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(nix_core PUBLIC Threads::Threads)
if(CMAKE_SYSTEM_NAME STREQUAL "QNX")
    target_link_libraries(nix_core PUBLIC socket)
endif()

apply_nix_warnings(nix_core)

install(TARGETS nix_core
    EXPORT nixTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)