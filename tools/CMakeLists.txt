cmake_minimum_required(VERSION 3.16)

project(DrasticLayoutEditor LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(IMGUI_DIR "" CACHE PATH "Path to a Dear ImGui checkout")

if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
    message(FATAL_ERROR "Set IMGUI_DIR to a Dear ImGui checkout, for example -DIMGUI_DIR=C:/dev/imgui")
endif()

if(NOT WIN32)
    message(FATAL_ERROR "The first version of this tool targets Windows only (Win32 + DirectX 11).")
endif()

add_executable(drastic_layout_editor WIN32
    src/main.cpp
    resources/app.rc
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_win32.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_dx11.cpp"
)

target_include_directories(drastic_layout_editor PRIVATE
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
)

target_link_libraries(drastic_layout_editor PRIVATE d3d11 dxgi d3dcompiler dwmapi imm32 ole32 windowscodecs comdlg32)

if(MINGW)
    target_link_options(drastic_layout_editor PRIVATE -static -static-libgcc -static-libstdc++)
endif()

if(MSVC)
    target_compile_options(drastic_layout_editor PRIVATE /W4 /permissive-)
else()
    target_compile_options(drastic_layout_editor PRIVATE -Wall -Wextra -Wpedantic)
endif()
