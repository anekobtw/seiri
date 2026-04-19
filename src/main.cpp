#include <windows.h>
#include <winuser.h>

#include <iostream>
#include <unordered_set>

#include "layout.h"

namespace {

constexpr UINT kMsgRelayout = WM_APP + 1;
constexpr DWORD kRelayoutDebounceMs = 120;
constexpr DWORD kDwmwaCloaked = 14;

DWORD g_lastLayoutTick = 0;
DWORD g_mainThreadId = 0;
bool g_isUserMovingWindow = false;
HWND g_moveSizeWindow = nullptr;
bool g_layoutQueued = false;
std::unordered_set<HWND> g_trackedWindows;
HWND g_anchorWindow = nullptr;
RECT g_anchorRect{};
bool g_hasAnchorRect = false;

}  // namespace

bool IsOnCurrentDesktop(HWND hwnd) {
  // i assume that if a window is on another desktop, then it's cloaked by DWM
  using DwmGetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);
  static DwmGetWindowAttributeFn pDwmGetWindowAttribute = nullptr;
  static bool initialized = false;

  if (!initialized) {
    HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
    if (dwm) {
      pDwmGetWindowAttribute = reinterpret_cast<DwmGetWindowAttributeFn>(
          GetProcAddress(dwm, "DwmGetWindowAttribute"));
    }
    initialized = true;
  }

  if (!pDwmGetWindowAttribute) return true;

  DWORD cloaked = 0;
  const HRESULT hr =
      pDwmGetWindowAttribute(hwnd, kDwmwaCloaked, &cloaked, sizeof(cloaked));

  if (FAILED(hr)) return true;
  return cloaked == 0;
}

bool IsWindowManageable(HWND hwnd) {
  const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

  if ((style & WS_THICKFRAME) == 0) return false;
  if (exStyle & WS_EX_TOOLWINDOW) return false;
  if (exStyle & WS_EX_NOACTIVATE) return false;

  return true;
}

bool IsWMWindow(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
  if (GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
  if (GetWindow(hwnd, GW_OWNER) != nullptr) return false;
  if (!IsOnCurrentDesktop(hwnd)) return false;
  if (!IsWindowManageable(hwnd)) return false;

  return true;
}

void QueueRelayout() {
  if (g_layoutQueued) return;

  g_layoutQueued = true;
  if (!PostThreadMessage(g_mainThreadId, kMsgRelayout, 0, 0)) {
    g_layoutQueued = false;
  }
}

BOOL CALLBACK SyncTrackedEnumProc(HWND hwnd, LPARAM lParam) {
  auto* out = reinterpret_cast<std::unordered_set<HWND>*>(lParam);
  if (!out) return TRUE;

  if (IsWMWindow(hwnd)) out->insert(hwnd);
  return TRUE;
}

void SyncTrackedWindows() {
  std::unordered_set<HWND> next;
  EnumWindows(SyncTrackedEnumProc, reinterpret_cast<LPARAM>(&next));
  g_trackedWindows.swap(next);
}

void ProcessRelayout() {
  g_layoutQueued = false;

  if (g_isUserMovingWindow) return;

  const DWORD now = GetTickCount();
  if ((now - g_lastLayoutTick) < kRelayoutDebounceMs) {
    return;
  }

  g_lastLayoutTick = now;
  SyncTrackedWindows();

  if (g_hasAnchorRect && (!g_anchorWindow || !IsWindow(g_anchorWindow) ||
                          !IsWMWindow(g_anchorWindow))) {
    g_anchorWindow = nullptr;
    g_hasAnchorRect = false;
  }

  RecalculateAndApplyLayout(IsWMWindow,
                            g_hasAnchorRect ? g_anchorWindow : nullptr,
                            g_hasAnchorRect ? &g_anchorRect : nullptr);
}

void CALLBACK WinEventHookProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                               LONG idObject, LONG idChild, DWORD, DWORD) {
  if (!hwnd) return;

  if (event == EVENT_SYSTEM_MOVESIZESTART) {
    if (IsWMWindow(hwnd)) {
      g_isUserMovingWindow = true;
      g_moveSizeWindow = hwnd;
    }
    return;
  }

  if (event == EVENT_SYSTEM_MOVESIZEEND) {
    if (g_moveSizeWindow == hwnd || g_moveSizeWindow == nullptr) {
      g_isUserMovingWindow = false;
      g_moveSizeWindow = nullptr;
    }

    if (IsWMWindow(hwnd) && GetWindowRect(hwnd, &g_anchorRect)) {
      g_anchorWindow = hwnd;
      g_hasAnchorRect = true;
    }

    QueueRelayout();
    return;
  }

  if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;

  if (event == EVENT_OBJECT_SHOW) {
    if (!IsWMWindow(hwnd)) return;

    const bool inserted = g_trackedWindows.insert(hwnd).second;
    if (!inserted) return;

    wchar_t title[256] = {0};
    GetWindowTextW(hwnd, title, 256);
    std::wcout << L"Window added: hwnd=" << hwnd << L", title='" << title
               << L"'" << std::endl;

    QueueRelayout();
    return;
  }

  if (event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_HIDE) {
    if (hwnd == g_anchorWindow) {
      g_anchorWindow = nullptr;
      g_hasAnchorRect = false;
    }

    if (hwnd == g_moveSizeWindow) {
      g_moveSizeWindow = nullptr;
      g_isUserMovingWindow = false;
    }

    if (g_trackedWindows.erase(hwnd) > 0) {
      QueueRelayout();
    }
  }
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  g_mainThreadId = GetCurrentThreadId();

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

  SyncTrackedWindows();
  QueueRelayout();

  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (msg.message == kMsgRelayout) {
      ProcessRelayout();
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
