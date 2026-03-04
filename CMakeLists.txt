cmake_minimum_required(VERSION 3.20)
project(Qmi LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    libwebp
    URL https://github.com/webmproject/libwebp/archive/refs/tags/v1.5.0.tar.gz
)
FetchContent_MakeAvailable(libwebp)

set(QMI_ICON_PNG "${CMAKE_SOURCE_DIR}/Qmi.png")
set(QMI_ICON_ICO "${CMAKE_BINARY_DIR}/Qmi.ico")
set(QMI_ICON_RC "${CMAKE_BINARY_DIR}/qmi_icon.rc")
set(QMI_ICON_ICO_RC_PATH "${QMI_ICON_ICO}")

if(NOT EXISTS "${QMI_ICON_PNG}")
    message(FATAL_ERROR "Missing app icon source: ${QMI_ICON_PNG}")
endif()

execute_process(
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass -File
            "${CMAKE_SOURCE_DIR}/tools/png_to_ico.ps1"
            -InputPng "${QMI_ICON_PNG}"
            -OutputIco "${QMI_ICON_ICO}"
    RESULT_VARIABLE QMI_ICON_RESULT
    OUTPUT_VARIABLE QMI_ICON_STDOUT
    ERROR_VARIABLE QMI_ICON_STDERR
)
if(NOT QMI_ICON_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to convert Qmi.png to icon.\n${QMI_ICON_STDOUT}\n${QMI_ICON_STDERR}")
endif()

configure_file("${CMAKE_SOURCE_DIR}/src/qmi_icon.rc.in" "${QMI_ICON_RC}" @ONLY)

add_executable(Qmi WIN32
    src/main.cpp
    src/qmi_actions.cpp
    src/qmi_imaging.cpp
    src/qmi_navigation.cpp
    src/qmi_settings.cpp
    src/qmi_utils.cpp
    src/qmi_view.cpp
    "${QMI_ICON_RC}"
)

target_compile_definitions(Qmi PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(Qmi PRIVATE
    webp
    d2d1
    d3d11
    dxgi
    dwmapi
    windowscodecs
    dwrite
    shell32
    shlwapi
    comdlg32
    ole32
)
