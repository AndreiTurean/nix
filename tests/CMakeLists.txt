if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    return()
endif()

add_executable(test_spsc_ring_buffer test_spsc_ring_buffer.cxx)
apply_nix_warnings(test_spsc_ring_buffer)
target_link_libraries(test_spsc_ring_buffer PRIVATE nix::core)
add_test(NAME SpscRingBufferBasicTest COMMAND test_spsc_ring_buffer)

add_executable(test_shm_channel test_shm_channel.cxx)
apply_nix_warnings(test_shm_channel)
target_link_libraries(test_shm_channel PRIVATE nix::core)
add_test(NAME ShmChannelTest COMMAND test_shm_channel)

add_executable(test_serialized_message test_serialized_message.cxx)
apply_nix_warnings(test_serialized_message)
target_link_libraries(test_serialized_message PRIVATE nix::core)
add_test(NAME SerializedMessagePortableFrameTest COMMAND test_serialized_message)

add_executable(test_logging test_logging.cxx)
apply_nix_warnings(test_logging)
target_link_libraries(test_logging PRIVATE nix::core)
add_test(NAME LoggingFormatTest COMMAND test_logging)

add_executable(test_shm_cleanup test_shm_cleanup.cxx)
apply_nix_warnings(test_shm_cleanup)
target_link_libraries(test_shm_cleanup PRIVATE nix::core)
add_test(NAME ShmCleanupTest COMMAND test_shm_cleanup $<TARGET_FILE:nix-cleanup-shm>)

add_executable(test_pub_sub_ipc test_pub_sub_ipc.cxx)
apply_nix_warnings(test_pub_sub_ipc)
target_link_libraries(test_pub_sub_ipc PRIVATE nix::core)
add_test(NAME PublisherSubcriberIpcTest COMMAND test_pub_sub_ipc $<TARGET_FILE:nix-daemon>)

add_executable(python_interop_peer python_interop_peer.cxx)
apply_nix_warnings(python_interop_peer)
target_link_libraries(python_interop_peer PRIVATE nix::core)