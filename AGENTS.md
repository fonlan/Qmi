# Qmi Project Notes (Current Implementation)

## Tech Stack

- Platform: Windows desktop only
- Language: C++20
- UI/Windowing: Win32 API (custom chrome, borderless main window)
- Rendering: Direct3D 11 + Direct2D 1.3/1.5
- Imaging: WIC for raster formats, Direct2D SVG document for `.svg`
- Build system: CMake (Visual Studio 2022 generator)

## Repository Layout

- `CMakeLists.txt`: single `WIN32` target `Qmi`, links D2D/D3D/WIC/DWM/Win32 libs.
- `src/main.cpp`: entire application implementation in one file (`QmiApp`).

## Current UI/Behavior

- Main window style: `WS_POPUP | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX`.
- No top title bar is drawn.
- Minimize / maximize / close buttons are custom-drawn in the top-right of the image viewport.
- Image viewport has zero outer margin/gap (flush to window content area) for a truly borderless look.
- Areas outside the image viewport are rendered as translucent overlays/panels with alpha `200/255` (~78%).
- Main window uses `WS_EX_LAYERED` with per-pixel alpha composition via `UpdateLayeredWindow`.
- In the image viewport, letterboxed area is rendered semi-transparent (non-zero alpha) to avoid mouse pass-through; no opaque underlay is drawn beneath image pixels, so transparent image content remains transparent (no black matte).
- Image drag/pan starts only when left click is on the currently visible image content (not just anywhere in viewport).
- Left-click drag on non-image UI regions (outside the currently visible image, excluding title buttons/thumbnails) moves the main window.
- Mouse wheel zoom is cursor-anchored and active only inside image viewport.
- Dragging/zooming repaint requests are throttled to ~60 FPS (16 ms minimum interval); idle state does not run a continuous render loop.
- Bottom filmstrip shows sibling images in current directory; click thumbnail to switch.
- Right-click context menu: `Open image...`, `Settings...`, `Exit`.
- Settings window toggles:
  - Fit-to-window when switching image
  - Smooth interpolation while zooming
- Keyboard:
  - `Left/Up`: previous image
  - `Right/Down`: next image
  - `0`: reset zoom/pan
- Drag-and-drop of image files onto main window is supported.

## Format Support

- Supported extensions: `.jpg`, `.jpeg`, `.png`, `.bmp`, `.webp`, `.svg`.
- WebP decoding depends on system WIC codec availability.

## Startup Behavior

- If a command-line image path is provided, Qmi tries to open it first.
- Otherwise, it scans the current working directory and opens the first supported image found.
- Startup uses a fast-open path: it first decodes/displays the target image, then defers full sibling-directory indexing to a startup timer so first paint is not blocked by large folder scans.

## Build

If `cmake` is in `PATH`:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

If `cmake` is not in `PATH`, use Visual Studio BuildTools bundled CMake:

```powershell
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S . -B build -G "Visual Studio 17 2022" -A x64
& "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build --config Release
```

Build output executable:

- `build/Release/Qmi.exe`

## Implementation Pointers

- App lifecycle / init: `QmiApp::Initialize`, `QmiApp::Run`
- Window creation/backdrop: `CreateMainWindow`, `ApplyWindowBackdrop`
- Image loading:
  - Raster: `LoadRasterBitmap`
  - SVG: `LoadSvgDocument`
  - File switch/open: `LoadImageByIndex`, `OpenImagePath`
  - Startup deferred indexing: `TryOpenInitialImage`, `ScheduleDeferredDirectoryBuild`, `BuildDirectoryList`
- Layout math:
  - `GetFilmStripRect`
  - `GetImageViewport`
  - `GetImageDestinationRect`
  - `GetTitleButtons`
- Rendering:
  - `Render`
  - `CreateWindowSizeResources` (offscreen texture + readback surface setup)
  - `PresentLayeredFrame` (copy GPU frame to DIB then `UpdateLayeredWindow`)
  - `DrawImageRegion`
  - `DrawFilmStrip`
  - `DrawTitleButtons`
- Input:
  - Zoom: `HandleMouseWheel`
  - Repaint throttling: `RequestRender` + `WM_TIMER(kRenderTimerId)`
  - Drag hit-test: `IsPointOverVisibleImage`
  - Window/event dispatch: `HandleMessage`

## Known Limitations

- Filmstrip thumbnails for SVG files are text placeholders (no SVG thumbnail rasterization).

## Documentation Sync Rules
- Every time you implement a new function or adjust a function, you need to check whether there are relevant instructions in `AGENTS.md`. If not, you need to add them.
- After each completed implementation or code adjustment, you must run a build to verify the project still compiles.
