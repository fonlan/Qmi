cmake_minimum_required(VERSION 3.20)
project(Qmi LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(Qmi WIN32
    src/main.cpp
)

target_compile_definitions(Qmi PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(Qmi PRIVATE
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
