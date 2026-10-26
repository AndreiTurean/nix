cmake_minimum_required(VERSION 3.22)
project(nix VERSION 0.1.0 LANGUAGES CXX)

# Enforce modern C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/nix/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/nix/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/nix/lib")
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(NIX_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Werror                  # Treat ALL warnings as errors
    -Wshadow                 # Warn when a variable shadows another local/global
    -Wnon-virtual-dtor       # Warn if a class with virtual functions has a non-virtual destructor
    -Wold-style-cast         # Warn on C-style casts (e.g. (int)x); forces static_cast/reinterpret_cast
    -Wcast-align             # Warn for pointer casts that increase alignment requirements
    -Wunused                 # Warn on anything unused (variables, functions, parameters)
    -Woverloaded-virtual     # Warn if you overload (not override) virtual functions
    -Wconversion             # Warn on implicit conversions that may alter values (e.g. int to char)
    -Wsign-conversion        # Warn on implicit conversions between signed and unsigned types
    -Wmisleading-indentation # Warn if indentation implies blocks where blocks do not exist
    -Wnull-dereference       # Warn if a null dereference is detected
    -Wuseless-cast           # Warn if you cast a type to itself
)

# GCC-specific extra strictness
set(NIX_GCC_EXTRA_WARNINGS
    -Wlogical-op             # Warn about suspicious uses of logical operators
    -Wduplicated-cond        # Warn about duplicated conditions in if-else chains
    -Wduplicated-branches      # Warn if multiple branches of an if/else have the same code
)

# Helper function to apply warnings to a target
function(apply_nix_warnings target_name)
    target_compile_options(${target_name} PRIVATE
        $<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:${NIX_WARNINGS}>
        $<$<CXX_COMPILER_ID:GNU>:${NIX_GCC_EXTRA_WARNINGS}>
    )
endfunction()

add_library(nix_core
  source/nix.cxx
)
add_library(nix::core ALIAS nix_core)

target_include_directories(nix_core
  PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
      $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

apply_nix_warnings(nix_core)

# Enable testing framework
enable_testing()

# Add the test executable
add_executable(test_spsc_ring_buffer tests/test_spsc_ring_buffer.cxx)
apply_nix_warnings(test_spsc_ring_buffer)
target_link_libraries(test_spsc_ring_buffer PRIVATE nix::core)
add_test(NAME SpscRingBufferBasicTest COMMAND test_spsc_ring_buffer)

add_executable(test_shm_channel tests/test_shm_channel.cxx)
apply_nix_warnings(test_shm_channel)
target_link_libraries(test_shm_channel PRIVATE nix::core)
add_test(NAME ShmChannelTest COMMAND test_shm_channel)

add_executable(test_pub_sub_ipc tests/test_pub_sub_ipc.cxx)
apply_nix_warnings(test_pub_sub_ipc)
target_link_libraries(test_pub_sub_ipc PRIVATE nix::core)
add_test(NAME PublisherSubcriberIpcTest COMMAND test_pub_sub_ipc)


### Install step
include(GNUInstallDirs)

install(TARGETS nix_core
    EXPORT nixTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT nixTargets
    FILE nixTargets.cmake
    NAMESPACE nix::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/nix
)
