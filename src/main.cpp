#include <windows.h>
#include <winuser.h>

#include <iostream>
#include <unordered_set>
#include <vector>

#include "layout.h"

namespace {

constexpr UINT WM_APP_RELAYOUT = WM_APP + 1;

DWORD g_lastLayoutTick = 0;
DWORD g_mainThreadId = 0;
bool g_isUserMovingWindow = false;
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
  constexpr DWORD DWMWA_CLOAKED_ATTR = 14;
  HRESULT hr = pDwmGetWindowAttribute(hwnd, DWMWA_CLOAKED_ATTR, &cloaked,
                                      sizeof(cloaked));
  if (FAILED(hr)) return true;

  return cloaked == 0;
}

bool IsWindowManagable(HWND hwnd) {
  LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

  if ((style & WS_THICKFRAME) == 0) return false;
  if (exStyle & WS_EX_TOOLWINDOW) return false;
  if (exStyle & WS_EX_NOACTIVATE) return false;

  return true;
}

bool IsWMWindow(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd)) return false;
  if (GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
  if (GetWindow(hwnd, GW_OWNER) != NULL) return false;
  if (!IsOnCurrentDesktop(hwnd)) return false;
  if (!IsWindowManagable(hwnd)) return false;

  return true;
}

void QueueRelayout() {
  if (g_layoutQueued) return;

  g_layoutQueued = true;
  PostThreadMessage(g_mainThreadId, WM_APP_RELAYOUT, 0, 0);
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

  DWORD now = GetTickCount();
  if ((now - g_lastLayoutTick) < 120) {
    QueueRelayout();
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
  if (!hwnd || !IsWindow(hwnd)) return;

  if (event == EVENT_SYSTEM_MOVESIZESTART) {
    if (IsWMWindow(hwnd)) g_isUserMovingWindow = true;
    return;
  }

  if (event == EVENT_SYSTEM_MOVESIZEEND) {
    g_isUserMovingWindow = false;

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

    if (g_trackedWindows.erase(hwnd) > 0) {
      QueueRelayout();
    }
    return;
  }
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  g_mainThreadId = GetCurrentThreadId();

  HWINEVENTHOOK showHook = SetWinEventHook(
      EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, NULL, WinEventHookProc, 0, 0,
      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  HWINEVENTHOOK hideHook = SetWinEventHook(
      EVENT_OBJECT_HIDE, EVENT_OBJECT_HIDE, NULL, WinEventHookProc, 0, 0,
      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  HWINEVENTHOOK destroyHook = SetWinEventHook(
      EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, NULL, WinEventHookProc, 0, 0,
      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  HWINEVENTHOOK moveSizeStartHook = SetWinEventHook(
      EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZESTART, NULL,
      WinEventHookProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  HWINEVENTHOOK moveSizeEndHook = SetWinEventHook(
      EVENT_SYSTEM_MOVESIZEEND, EVENT_SYSTEM_MOVESIZEEND, NULL,
      WinEventHookProc, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

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
  while (GetMessage(&msg, NULL, 0, 0)) {
    if (msg.message == WM_APP_RELAYOUT) {
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
