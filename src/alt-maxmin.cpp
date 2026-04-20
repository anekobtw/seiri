#include "alt-maxmin.h"

namespace {
HHOOK g_hook = nullptr;
AltMaxMinWindowFilterFn g_filter = nullptr;
DWORD g_lastTap = 0;
constexpr DWORD kDoubleTapMs = 350;

bool IsCandidateWindow(HWND hwnd) {
  return hwnd && IsWindowVisible(hwnd) && !IsIconic(hwnd) &&
         !(GetWindowLongPtr(hwnd, GWL_EXSTYLE) & (WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) &&
         (GetWindowLongPtr(hwnd, GWL_STYLE) & WS_THICKFRAME) &&
         !GetWindow(GetAncestor(hwnd, GA_ROOT), GW_OWNER) &&
         (!g_filter || g_filter(GetAncestor(hwnd, GA_ROOT)));
}

void HandleAltTap() {
  DWORD now = GetTickCount();
  bool isDoubleTap = (now - g_lastTap <= kDoubleTapMs);
  g_lastTap = now;

  if (!isDoubleTap)
    return;

  HWND hwnd = GetAncestor(GetForegroundWindow(), GA_ROOT);
  if (!IsCandidateWindow(hwnd))
    return;

  ShowWindow(hwnd, IsZoomed(hwnd) ? SW_MINIMIZE : SW_MAXIMIZE);
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
  if (code >= 0) {
    auto *k = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    if ((k->vkCode == VK_MENU || k->vkCode == VK_LMENU || k->vkCode == VK_RMENU) &&
        (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN))
      HandleAltTap();
  }
  return CallNextHookEx(g_hook, code, wParam, lParam);
}

void ResetState() {
  g_filter = nullptr;
  g_lastTap = 0;
}

} // namespace

bool InstallAltMaxMinHook(AltMaxMinWindowFilterFn filter) {
  if (g_hook)
    return true;

  g_filter = filter;
  g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(nullptr), 0);

  if (!g_hook) {
    ResetState();
    return false;
  }
  return true;
}

void UninstallAltMaxMinHook() {
  if (g_hook) {
    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
  }
  ResetState();
}
