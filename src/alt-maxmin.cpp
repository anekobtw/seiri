#include "alt-maxmin.h"

namespace {

HHOOK g_keyboardHook = nullptr;
AltMaxMinWindowFilterFn g_filter = nullptr;
DWORD g_lastAltTapTick = 0;
bool g_altDown = false;

constexpr DWORD kAltDoubleTapMs = 350;

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

HWND ResolveTargetWindow() {
  const HWND hwnd = GetAncestor(GetForegroundWindow(), GA_ROOT);
  return IsCandidateWindow(hwnd) ? hwnd : nullptr;
}

void TryHandleAltTap() {
  const DWORD now = GetTickCount();
  const DWORD elapsed = now - g_lastAltTapTick;
  g_lastAltTapTick = now;

  if (elapsed > kAltDoubleTapMs) return;

  const HWND hwnd = ResolveTargetWindow();
  if (!hwnd) return;

  ShowWindow(hwnd, IsZoomed(hwnd) ? SW_MINIMIZE : SW_MAXIMIZE);
}

LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode < 0 || !lParam)
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);

  const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
  if (key->vkCode != VK_MENU && key->vkCode != VK_LMENU &&
      key->vkCode != VK_RMENU) {
    return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
  }

  if ((key->flags & LLKHF_UP) == 0) {
    if (!g_altDown && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
      g_altDown = true;
      TryHandleAltTap();
    }
  } else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
    g_altDown = false;
  }

  return CallNextHookEx(g_keyboardHook, nCode, wParam, lParam);
}

}  // namespace

bool InstallAltMaxMinHook(AltMaxMinWindowFilterFn filter) {
  if (g_keyboardHook) return true;

  g_filter = filter;
  g_keyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                    GetModuleHandle(nullptr), 0);

  if (g_keyboardHook) return true;

  g_filter = nullptr;
  g_lastAltTapTick = 0;
  g_altDown = false;
  return false;
}

void UninstallAltMaxMinHook() {
  if (g_keyboardHook) {
    UnhookWindowsHookEx(g_keyboardHook);
    g_keyboardHook = nullptr;
  }

  g_filter = nullptr;
  g_lastAltTapTick = 0;
  g_altDown = false;
}
