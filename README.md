# Qmi

Qmi is a native Windows image viewer built with C++20, Win32, Direct3D 11, and Direct2D.
It focuses on a lightweight, borderless UI with smooth local image browsing.

## Highlights

- Borderless window with custom caption buttons
- Zoom (mouse-wheel, cursor-anchored) and pan (drag on visible image)
- Bottom filmstrip for sibling images in the same folder
- GIF playback in the main viewport
- Built-in WebP decoding via `libwebp`
- Settings window for viewer options and per-extension file associations

## Supported Formats

`.jpg` `.jpeg` `.png` `.bmp` `.ico` `.webp` `.gif` `.heic` `.heif` `.svg`

Notes:
- WebP is decoded with built-in `libwebp` (no system WebP codec required).
- HEIC/HEIF depend on system WIC codec availability.

## Build

Requirements:
- Windows 10/11 x64
- Visual Studio 2022 (Desktop C++ workload)
- PowerShell

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Output:
- `build/Release/Qmi.exe`

## Usage

```powershell
build/Release/Qmi.exe
# or
build/Release/Qmi.exe <image-path>
```

Shortcuts:
- `Left/Up`: Previous image
- `Right/Down`: Next image
- `0`: Reset zoom/pan

## Project Layout

- `src/main.cpp` - Main application implementation (`QmiApp`)
- `src/qmi_icon.rc.in` - Windows icon resource template
- `tools/png_to_ico.ps1` - Converts `Qmi.png` to multi-size ICO
- `CMakeLists.txt` - Build configuration
