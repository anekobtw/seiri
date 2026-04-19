#include <windows.h>
#include <winuser.h>

#include <algorithm>
#include <iostream>
#include <unordered_set>

#include "animations.h"
#include "layout.h"

namespace {

constexpr UINT kRelayoutMessage = WM_APP + 1;
constexpr DWORD kRelayoutDebounceMs = 120;
constexpr int kLayoutAnimationMs = 130;
constexpr int kOpenWindowAnimationMs = 180;
constexpr UINT_PTR kOpenWindowTimerId = 1;
constexpr DWORD kDwAttributeCloaked = 14;

DWORD g_lastRelayoutTick = 0;
bool g_isMoveSizeActive = false;
HWND g_moveSizeHwnd = nullptr;
bool g_isRelayoutQueued = false;
std::unordered_set<HWND> g_managedWindows;
HWND g_anchorHwnd = nullptr;
RECT g_anchorRect{};
bool g_hasAnchorRect = false;
HWND g_pendingOpenHwnd = nullptr;
RECT g_pendingOpenTargetRect{};
bool g_hasPendingOpenTargetRect = false;
bool g_pendingOpenStage = false;

}  // namespace

bool IsWMWindow(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
  if (GetAncestor(hwnd, GA_ROOT) != hwnd || GetWindow(hwnd, GW_OWNER))
    return false;

  const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  if (!(style & WS_THICKFRAME) || (exStyle & WS_EX_TOOLWINDOW) ||
      (exStyle & WS_EX_NOACTIVATE))
    return false;

  using DwmGetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
  static DwmGetWindowAttributeFn getWindowAttribute = nullptr;
  static bool initialized = false;

  if (!initialized) {
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
      getWindowAttribute = reinterpret_cast<DwmGetWindowAttributeFn>(
          GetProcAddress(dwm, "DwmGetWindowAttribute"));
    }
    initialized = true;
  }

  if (!getWindowAttribute) return true;

  DWORD cloaked = 0;
  const HRESULT hr =
      getWindowAttribute(hwnd, kDwAttributeCloaked, &cloaked, sizeof(cloaked));

  return FAILED(hr) || cloaked == 0;
}

void QueueRelayout() {
  if (g_isRelayoutQueued) return;

  g_isRelayoutQueued = true;
  if (!PostThreadMessage(GetCurrentThreadId(), kRelayoutMessage, 0, 0)) {
    g_isRelayoutQueued = false;
  }
}

BOOL CALLBACK SyncManagedEnumProc(HWND hwnd, LPARAM lParam) {
  auto* out = reinterpret_cast<std::unordered_set<HWND>*>(lParam);
  if (out && IsWMWindow(hwnd)) out->insert(hwnd);
  return TRUE;
}

void CALLBACK WinEventHookProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                               LONG idObject, LONG idChild, DWORD, DWORD) {
  if (!hwnd) return;

  if (event == EVENT_SYSTEM_MOVESIZESTART) {
    if (IsWMWindow(hwnd)) {
      g_isMoveSizeActive = true;
      g_moveSizeHwnd = hwnd;
    }
    return;
  }

  if (event == EVENT_SYSTEM_MOVESIZEEND) {
    if (g_moveSizeHwnd == hwnd || g_moveSizeHwnd == nullptr) {
      g_isMoveSizeActive = false;
      g_moveSizeHwnd = nullptr;
    }

    if (IsWMWindow(hwnd) && GetWindowRect(hwnd, &g_anchorRect)) {
      g_anchorHwnd = hwnd;
      g_hasAnchorRect = true;
    }

    QueueRelayout();
    return;
  }

  if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;

  if (event == EVENT_OBJECT_SHOW) {
    if (!IsWMWindow(hwnd) || !g_managedWindows.insert(hwnd).second) return;

    g_pendingOpenHwnd = hwnd;
    g_hasPendingOpenTargetRect = false;
    g_pendingOpenStage = true;

    wchar_t title[256] = {0};
    GetWindowTextW(hwnd, title, 256);
    std::wcout << L"Window added: hwnd=" << hwnd << L", title='" << title
               << L"'" << std::endl;

    QueueRelayout();
    return;
  }

  if (event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_HIDE) {
    if (hwnd == g_anchorHwnd) {
      g_anchorHwnd = nullptr;
      g_hasAnchorRect = false;
    }

    if (hwnd == g_moveSizeHwnd) {
      g_moveSizeHwnd = nullptr;
      g_isMoveSizeActive = false;
    }

    if (hwnd == g_pendingOpenHwnd) {
      g_pendingOpenHwnd = nullptr;
      g_pendingOpenStage = false;
      g_hasPendingOpenTargetRect = false;
      KillTimer(nullptr, kOpenWindowTimerId);
    }

    if (g_managedWindows.erase(hwnd) > 0) QueueRelayout();
  }
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  MSG initMsg;
  PeekMessage(&initMsg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

  const DWORD hookFlags = WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS;

  HWINEVENTHOOK showHook =
      SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, nullptr,
                      WinEventHookProc, 0, 0, hookFlags);

  HWINEVENTHOOK hideHook =
      SetWinEventHook(EVENT_OBJECT_HIDE, EVENT_OBJECT_HIDE, nullptr,
                      WinEventHookProc, 0, 0, hookFlags);

  HWINEVENTHOOK destroyHook =
      SetWinEventHook(EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, nullptr,
                      WinEventHookProc, 0, 0, hookFlags);

  HWINEVENTHOOK moveSizeStartHook =
      SetWinEventHook(EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZESTART,
                      nullptr, WinEventHookProc, 0, 0, hookFlags);

  HWINEVENTHOOK moveSizeEndHook =
      SetWinEventHook(EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND,
                      nullptr, WinEventHookProc, 0, 0, hookFlags);

  if (!showHook || !hideHook || !destroyHook || !moveSizeStartHook ||
      !moveSizeEndHook) {
    if (showHook) UnhookWinEvent(showHook);
    if (hideHook) UnhookWinEvent(hideHook);
    if (destroyHook) UnhookWinEvent(destroyHook);
    if (moveSizeStartHook) UnhookWinEvent(moveSizeStartHook);
    if (moveSizeEndHook) UnhookWinEvent(moveSizeEndHook);
    return 1;
  }

  EnumWindows(SyncManagedEnumProc, reinterpret_cast<LPARAM>(&g_managedWindows));
  QueueRelayout();

  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (msg.message == kRelayoutMessage) {
      g_isRelayoutQueued = false;

      if (g_isMoveSizeActive) continue;

      const DWORD now = GetTickCount();
      if ((now - g_lastRelayoutTick) < kRelayoutDebounceMs) continue;
      g_lastRelayoutTick = now;

      std::unordered_set<HWND> next;
      EnumWindows(SyncManagedEnumProc, reinterpret_cast<LPARAM>(&next));
      g_managedWindows.swap(next);

      if (g_hasAnchorRect && (!g_anchorHwnd || !IsWindow(g_anchorHwnd) ||
                              !IsWMWindow(g_anchorHwnd))) {
        g_anchorHwnd = nullptr;
        g_hasAnchorRect = false;
      }

      HWND anchor = g_hasAnchorRect ? g_anchorHwnd : nullptr;
      const RECT* anchorRect = g_hasAnchorRect ? &g_anchorRect : nullptr;

      if (g_pendingOpenStage && g_pendingOpenHwnd &&
          g_managedWindows.find(g_pendingOpenHwnd) != g_managedWindows.end()) {
        RECT target{};
        RecalculateAndApplyLayout(IsWMWindow, anchor, anchorRect,
                                  kLayoutAnimationMs, g_pendingOpenHwnd,
                                  &target);

        g_pendingOpenTargetRect = target;
        g_hasPendingOpenTargetRect = true;
        g_pendingOpenStage = false;
        SetTimer(nullptr, kOpenWindowTimerId, kLayoutAnimationMs, nullptr);
        continue;
      }

      RecalculateAndApplyLayout(IsWMWindow, anchor, anchorRect,
                                kLayoutAnimationMs);
      continue;
    }

    if (msg.message == WM_TIMER && msg.wParam == kOpenWindowTimerId) {
      KillTimer(nullptr, kOpenWindowTimerId);

      if (g_pendingOpenHwnd && g_hasPendingOpenTargetRect &&
          IsWindow(g_pendingOpenHwnd) && IsWMWindow(g_pendingOpenHwnd)) {
        const int targetW =
            g_pendingOpenTargetRect.right - g_pendingOpenTargetRect.left;
        const int targetH =
            g_pendingOpenTargetRect.bottom - g_pendingOpenTargetRect.top;

        const int startW = std::max(120, (targetW * 65) / 100);
        const int startH = std::max(90, (targetH * 65) / 100);
        const int centerX = g_pendingOpenTargetRect.left + targetW / 2;
        const int centerY = g_pendingOpenTargetRect.top + targetH / 2;
        const int startX = centerX - startW / 2;
        const int startY = centerY - startH / 2;

        SetWindowPos(g_pendingOpenHwnd, nullptr, startX, startY, startW, startH,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);

        AnimateWindowTransform(g_pendingOpenHwnd, g_pendingOpenTargetRect.left,
                               g_pendingOpenTargetRect.top, targetW, targetH,
                               kOpenWindowAnimationMs);
      }

      g_pendingOpenHwnd = nullptr;
      g_hasPendingOpenTargetRect = false;
      continue;
    }

    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UnhookWinEvent(showHook);
  UnhookWinEvent(hideHook);
  UnhookWinEvent(destroyHook);
  UnhookWinEvent(moveSizeStartHook);
  UnhookWinEvent(moveSizeEndHook);
  return 0;
}
