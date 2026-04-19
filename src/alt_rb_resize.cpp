#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <algorithm>

// Minimal API for integration:
// - filter: returns true for windows managed by your WM
// - onCommit: called once when resize gesture ends
using AltSnapResizeWindowFilterFn = bool (*)(HWND);
using AltSnapResizeCommitFn = void (*)(HWND, const RECT&);

namespace {

constexpr int kMinWindowWidth = 140;
constexpr int kMinWindowHeight = 100;

enum ResizeEdges : unsigned {
  kEdgeNone = 0,
  kEdgeLeft = 1 << 0,
  kEdgeRight = 1 << 1,
  kEdgeTop = 1 << 2,
  kEdgeBottom = 1 << 3,
};

HHOOK g_mouseHook = nullptr;
AltSnapResizeWindowFilterFn g_filter = nullptr;
AltSnapResizeCommitFn g_onCommit = nullptr;

bool g_isResizing = false;
HWND g_targetHwnd = nullptr;
POINT g_startCursor{};
RECT g_startRect{};
unsigned g_activeEdges = kEdgeNone;

inline bool IsAltDown() { return (GetAsyncKeyState(VK_MENU) & 0x8000) != 0; }
inline bool IsLeftMouseDown() {
  return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
}
inline int Width(const RECT& r) { return r.right - r.left; }
inline int Height(const RECT& r) { return r.bottom - r.top; }

bool IsEligibleTopLevelWindow(HWND hwnd) {
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

unsigned ResolveEdgesForStartPoint(const RECT& rect, const POINT& cursor) {
  const int cx = rect.left + Width(rect) / 2;
  const int cy = rect.top + Height(rect) / 2;
  return (cursor.x < cx ? kEdgeLeft : kEdgeRight) |
         (cursor.y < cy ? kEdgeTop : kEdgeBottom);
}

RECT ComputeResizedRect(const RECT& startRect, const POINT& startCursor,
                        const POINT& currentCursor, unsigned edges) {
  RECT out = startRect;
  const int dx = currentCursor.x - startCursor.x;
  const int dy = currentCursor.y - startCursor.y;

  if (edges & kEdgeLeft) out.left = startRect.left + dx;
  if (edges & kEdgeRight) out.right = startRect.right + dx;
  if (edges & kEdgeTop) out.top = startRect.top + dy;
  if (edges & kEdgeBottom) out.bottom = startRect.bottom + dy;

  if (Width(out) < kMinWindowWidth)
    (edges & kEdgeLeft) ? out.left = out.right - kMinWindowWidth
                        : out.right = out.left + kMinWindowWidth;

  if (Height(out) < kMinWindowHeight)
    (edges & kEdgeTop) ? out.top = out.bottom - kMinWindowHeight
                       : out.bottom = out.top + kMinWindowHeight;

  return out;
}

void EndResize(bool notifyCommit) {
  if (!g_isResizing) return;

  const HWND hwnd = g_targetHwnd;
  g_isResizing = false;
  g_targetHwnd = nullptr;
  g_activeEdges = kEdgeNone;

  if (!notifyCommit || !g_onCommit || !hwnd || !IsWindow(hwnd)) return;
  RECT finalRect{};
  if (GetWindowRect(hwnd, &finalRect)) g_onCommit(hwnd, finalRect);
}

void UpdateResizeFromCursor(const POINT& cursor) {
  if (!g_isResizing || !g_targetHwnd || !IsWindow(g_targetHwnd))
    return EndResize(false);

  const RECT nextRect =
      ComputeResizedRect(g_startRect, g_startCursor, cursor, g_activeEdges);

  SetWindowPos(
      g_targetHwnd, nullptr, nextRect.left, nextRect.top, Width(nextRect),
      Height(nextRect),
      SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOSENDCHANGING);
}

LRESULT CALLBACK LowLevelMouseProc(int code, WPARAM wParam, LPARAM lParam) {
  if (code < 0 || !lParam)
    return CallNextHookEx(g_mouseHook, code, wParam, lParam);

  const auto* info = reinterpret_cast<const MSLLHOOKSTRUCT*>(lParam);

  if (wParam == WM_LBUTTONDOWN && !g_isResizing && IsAltDown()) {
    const HWND hwnd = GetAncestor(WindowFromPoint(info->pt), GA_ROOT);
    RECT rect{};
    if (IsEligibleTopLevelWindow(hwnd) && GetWindowRect(hwnd, &rect)) {
      g_isResizing = true;
      g_targetHwnd = hwnd;
      g_startCursor = info->pt;
      g_startRect = rect;
      g_activeEdges = ResolveEdgesForStartPoint(rect, info->pt);
      return 1;
    }
  }

  if (!g_isResizing) return CallNextHookEx(g_mouseHook, code, wParam, lParam);

  if (!IsAltDown() || !IsLeftMouseDown()) {
    EndResize(true);
  } else if (wParam == WM_MOUSEMOVE) {
    UpdateResizeFromCursor(info->pt);
  } else if (wParam == WM_LBUTTONUP) {
    EndResize(true);
  }

  return 1;
}

}  // namespace

bool InstallAltSnapResizeHook(AltSnapResizeWindowFilterFn filter,
                              AltSnapResizeCommitFn onCommit) {
  if (g_mouseHook) return true;

  g_filter = filter;
  g_onCommit = onCommit;
  g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc,
                                  GetModuleHandleW(nullptr), 0);

  if (g_mouseHook) return true;
  g_filter = nullptr;
  g_onCommit = nullptr;
  return false;
}

void UninstallAltSnapResizeHook() {
  EndResize(false);
  if (g_mouseHook) {
    UnhookWindowsHookEx(g_mouseHook);
    g_mouseHook = nullptr;
  }
  g_filter = nullptr;
  g_onCommit = nullptr;
}
