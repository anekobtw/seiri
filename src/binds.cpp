#include "binds.h"

#include "layout_controller.h"
#include "state.h"

namespace {

HHOOK g_hook = nullptr;
BindsWindowFilterFn g_filter = nullptr;
AppState *g_state = nullptr;
UINT g_superKey = VK_MENU;
bool g_superFDown = false;

HWND GetManagedForegroundRoot() {
  const HWND root = GetAncestor(GetForegroundWindow(), GA_ROOT);
  if (!root || !g_filter || !g_filter(root))
    return nullptr;
  return root;
}

void ToggleFullscreenTileForForeground() {
  if (!g_state)
    return;

  const HWND root = GetManagedForegroundRoot();
  if (!root)
    return;

  if (g_state->fullscreenHwnd == root)
    g_state->fullscreenHwnd = nullptr;
  else
    g_state->fullscreenHwnd = root;

  QueueRelayout(*g_state);
}

bool IsAltVirtualKey(UINT vk) {
  return vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU;
}

bool IsSuperPressed(const KBDLLHOOKSTRUCT *keyEvent) {
  if (IsAltVirtualKey(g_superKey)) {
    return (keyEvent->flags & LLKHF_ALTDOWN) != 0 || (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
  }

  return (GetAsyncKeyState(static_cast<int>(g_superKey)) & 0x8000) != 0;
}

LRESULT CALLBACK KeyboardProc(int code, WPARAM wParam, LPARAM lParam) {
  if (code >= 0) {
    auto *keyEvent = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    const bool isFKey = keyEvent->vkCode == 'F';
    const bool isDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool isUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);

    if (isFKey && isDown && IsSuperPressed(keyEvent)) {
      if (!g_superFDown) {
        g_superFDown = true;
        ToggleFullscreenTileForForeground();
      }
      return 1;
    }

    if (isFKey && isUp)
      g_superFDown = false;
  }

  return CallNextHookEx(g_hook, code, wParam, lParam);
}

void ResetState() {
  g_filter = nullptr;
  g_state = nullptr;
  g_superKey = VK_MENU;
  g_superFDown = false;
}

} // namespace

bool InstallBindsHook(BindsWindowFilterFn filter, UINT superKey, AppState *state) {
  if (g_hook)
    return true;

  g_filter = filter;
  g_state = state;
  g_superKey = superKey ? superKey : VK_MENU;
  g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, KeyboardProc, GetModuleHandle(nullptr), 0);

  if (!g_hook) {
    ResetState();
    return false;
  }

  return true;
}

void UninstallBindsHook() {
  if (g_hook) {
    UnhookWindowsHookEx(g_hook);
    g_hook = nullptr;
  }

  ResetState();
}
