set(IMGUI_DIR "${CMAKE_SOURCE_DIR}/third_party/imgui")
add_executable(nix-daemon-gui
    main.cxx
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_qnx.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
target_include_directories(nix-daemon-gui PRIVATE
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_compile_definitions(nix-daemon-gui PRIVATE VK_USE_PLATFORM_SCREEN_QNX=1)
target_link_libraries(nix-daemon-gui PRIVATE nix::core vulkan screen m)

install(TARGETS nix-daemon-gui RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})