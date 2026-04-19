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

bool IsAltDown() { return (GetAsyncKeyState(VK_MENU) & 0x8000) != 0; }

bool IsLeftDown() { return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0; }

bool IsCandidateWindow(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd))
    return false;

  hwnd = GetAncestor(hwnd, GA_ROOT);
  if (!hwnd) return false;

  if (GetAncestor(hwnd, GA_ROOT) != hwnd || GetWindow(hwnd, GW_OWNER))
    return false;

  const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  if (!(style & WS_THICKFRAME) || (exStyle & WS_EX_TOOLWINDOW) ||
      (exStyle & WS_EX_NOACTIVATE))
    return false;

  if (g_filter && !g_filter(hwnd)) return false;
  return true;
}

void StopResize(bool notifyCommit) {
  if (!g_isResizing) return;

  HWND finalHwnd = g_targetHwnd;
  g_isResizing = false;
  g_targetHwnd = nullptr;

  if (!notifyCommit || !g_onCommit || !finalHwnd || !IsWindow(finalHwnd))
    return;

  RECT finalRect{};
  if (GetWindowRect(finalHwnd, &finalRect)) g_onCommit(finalHwnd, finalRect);
}

void ApplyResizeFromCursor() {
  if (!g_isResizing || !g_targetHwnd || !IsWindow(g_targetHwnd)) {
    StopResize(false);
    return;
  }

  POINT cursor{};
  if (!GetCursorPos(&cursor)) return;

  const int dx = cursor.x - g_startCursor.x;
  const int dy = cursor.y - g_startCursor.y;

  const int startWidth = g_startRect.right - g_startRect.left;
  const int startHeight = g_startRect.bottom - g_startRect.top;

  const int targetWidth = std::max(kMinResizeWidth, startWidth + dx);
  const int targetHeight = std::max(kMinResizeHeight, startHeight + dy);

  SetWindowPos(g_targetHwnd, nullptr, g_startRect.left, g_startRect.top,
               targetWidth, targetHeight,
               SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode < 0 || !lParam)
    return CallNextHookEx(g_mouseHook, nCode, wParam, lParam);

  const auto* mouse = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);

  if (wParam == WM_LBUTTONDOWN && !g_isResizing && IsAltDown()) {
    HWND hwnd = WindowFromPoint(mouse->pt);
    hwnd = GetAncestor(hwnd, GA_ROOT);

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
    return 1;
  }

  if (wParam == WM_MOUSEMOVE) {
    ApplyResizeFromCursor();
    return 1;
  }

  if (wParam == WM_LBUTTONUP) {
    StopResize(true);
    return 1;
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

  if (!g_mouseHook) {
    g_filter = nullptr;
    g_onCommit = nullptr;
    return false;
  }

  return true;
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
