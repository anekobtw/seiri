# Seiri

Seiri is a lightweight, event-driven tiling window manager for Microsoft Windows.
It listens to native Win32 window events, computes a per-monitor tiled layout, and applies animated window transforms with minimal runtime overhead.

## Build

There is currently no committed build system file (no CMake/project file in this repository).
Compile directly from source.

### MinGW-w64 (g++)

```bash
g++ -std=c++17 -O2 -mwindows src/main.cpp src/window_manager.cpp src/hooks.cpp src/layout_controller.cpp src/animation_controller.cpp src/layout.cpp src/animations.cpp src/alt-maxmin.cpp -o seiri.exe -luser32 -lgdi32
```

### MSVC (Developer Command Prompt)

```bat
cl /std:c++17 /EHsc /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN src\main.cpp src\window_manager.cpp src\hooks.cpp src\layout_controller.cpp src\animation_controller.cpp src\layout.cpp src\animations.cpp src\alt-maxmin.cpp /link user32.lib gdi32.lib
```

## Run

Start the built executable and keep it running in the background. Seiri will automatically respond to supported window lifecycle and state events.

## Current Scope

- Windows-only implementation (Win32 APIs)
- No tray UI or configuration file in current revision
- No per-application rules/exclusion list yet
- No automated test suite in repository yet

## License

MIT License. See [LICENSE](LICENSE).
