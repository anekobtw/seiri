#include "alt_resize.h"

#include <algorithm>

namespace {

constexpr int kMinResizeWidth = 140;
constexpr int kMinResizeHeight = 100;

HHOOK g_mouseHook = nullptr;
bool g_isResizing = false;
HWND g_targetHwnd = nullptr;
POINT g_startCursor{};
RECT g_startRect{};
AltResizeWindowFilterFn g_filter = nullptr;
AltResizeCommitFn g_onCommit = nullptr;

inline bool IsAltDown() { return (GetAsyncKeyState(VK_MENU) & 0x8000) != 0; }
inline bool IsLeftDown() {
  return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}

bool IsCandidateWindow(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd))
    return false;
  hwnd = GetAncestor(hwnd, GA_ROOT);
  if (!hwnd || GetAncestor(hwnd, GA_ROOT) != hwnd || GetWindow(hwnd, GW_OWNER))
    return false;

  const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  if (!(style & WS_THICKFRAME) || (exStyle & WS_EX_TOOLWINDOW) ||
      (exStyle & WS_EX_NOACTIVATE))
    return false;

  return !g_filter || g_filter(hwnd);
}

void StopResize(bool notifyCommit) {
  if (!g_isResizing) return;
  const HWND hwnd = g_targetHwnd;
  g_isResizing = false;
  g_targetHwnd = nullptr;

  if (!notifyCommit || !g_onCommit || !hwnd || !IsWindow(hwnd)) return;
  RECT rect{};
  if (GetWindowRect(hwnd, &rect)) g_onCommit(hwnd, rect);
}

void ApplyResizeFromCursor() {
  if (!g_isResizing || !g_targetHwnd || !IsWindow(g_targetHwnd))
    return StopResize(false);

  POINT cursor{};
  if (!GetCursorPos(&cursor)) return;

  const int w = std::max(kMinResizeWidth,
                         static_cast<int>(g_startRect.right - g_startRect.left +
                                          (cursor.x - g_startCursor.x)));
  const int h = std::max(kMinResizeHeight,
                         static_cast<int>(g_startRect.bottom - g_startRect.top +
                                          (cursor.y - g_startCursor.y)));

  SetWindowPos(g_targetHwnd, nullptr, g_startRect.left, g_startRect.top, w, h,
               SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode < 0 || !lParam)
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);

  const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);

  if (wParam == WM_LBUTTONDOWN && !g_isResizing && IsAltDown()) {
    const HWND hwnd = GetAncestor(WindowFromPoint(mouse->pt), GA_ROOT);
    if (IsCandidateWindow(hwnd) && GetWindowRect(hwnd, &g_startRect)) {
      g_targetHwnd = hwnd;
      g_startCursor = mouse->pt;
      g_isResizing = true;
      return 1;
    }
  }

  if (!g_isResizing) return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);

  if (!IsAltDown() || !IsLeftDown()) {
    StopResize(true);
  } else if (wParam == WM_MOUSEMOVE) {
    ApplyResizeFromCursor();
  } else if (wParam == WM_LBUTTONUP) {
    StopResize(true);
  }

  return 1;
}

}  // namespace

bool InstallAltResizeHook(AltResizeWindowFilterFn filter,
                          AltResizeCommitFn onCommit) {
  if (g_mouseHook) return true;

  g_filter = filter;
  g_onCommit = onCommit;
  g_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc,
                                 GetModuleHandle(nullptr), 0);

  if (g_mouseHook) return true;
  g_filter = nullptr;
  g_onCommit = nullptr;
  return false;
}

void UninstallAltResizeHook() {
  StopResize(false);
  if (g_mouseHook) {
    UnhookWindowsHookEx(g_mouseHook);
    g_mouseHook = nullptr;
  }
  g_filter = nullptr;
  g_onCommit = nullptr;
}
