add_executable(counter_publisher counter_publisher.cxx)
target_link_libraries(counter_publisher PRIVATE nix::core)
apply_nix_warnings(counter_publisher)

add_executable(counter_subscriber counter_subscriber.cxx)
target_link_libraries(counter_subscriber PRIVATE nix::core)
apply_nix_warnings(counter_subscriber)

add_executable(versioned_message_example versioned_message.cxx)
target_link_libraries(versioned_message_example PRIVATE nix::core)
apply_nix_warnings(versioned_message_example)

install(TARGETS counter_publisher counter_subscriber versioned_message_example
    EXPORT nixTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)