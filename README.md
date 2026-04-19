# Seiri

Seiri is a lightweight, event-driven tiling window manager for Microsoft Windows.
It monitors top-level application windows, computes an adaptive tiled layout per monitor, and applies animated repositioning and resizing in response to window lifecycle and state changes.

The project is implemented in modern C++ with direct Win32 API integration and no third-party runtime dependencies.

## Key Capabilities

- **Automatic tiling layout** for eligible top-level windows.
- **Per-monitor layout buckets** using each monitor's work area.
- **Dwindle-style recursive split strategy** for multi-window arrangements.
- **Anchor-aware relayout** to preserve a moved/resized window while redistributing remaining windows.
- **Debounced event-driven recomputation** using WinEvent hooks.
- **Smooth window transition animations** for layout changes and newly opened windows.
- **ALT double-tap maximize/minimize toggle** for the focused window.
- **Filtering of unmanaged windows** (tool windows, no-activate windows, cloaked windows, minimized windows, etc.).

## Build

There is currently **no committed build system file** (no CMake/project file in this repository). Build from sources directly with your compiler.

### Option B: MinGW-w64 (g++)

```bash
g++ -std=c++17 -O2 -municode -mwindows \
  src/main.cpp src/layout.cpp src/animations.cpp src/alt-maxmin.cpp \
  -o seiri.exe -luser32 -lgdi32
```

Notes:

- If you prefer a console build for diagnostics, remove the GUI subsystem flag (`-mwindows`) and adapt entry point settings as needed.

## Controls and Behavior

- **ALT double-tap** on the focused managed window: maximize/minimize toggle.
- Manual move/resize operations are respected and relayout remains event-driven.
- Newly shown windows are inserted with a staged open animation.

## Current Constraints

- Windows platform only (Win32 hooks, monitor APIs, DWM attributes).
- No tray UI, configuration file, or persistent settings in this revision.
- No exclusion rules or per-application policies yet.
- No automated tests or CI workflow are currently included.

## License

MIT License. See [LICENSE](LICENSE).
