#include <windows.h>
#include <winuser.h>

#include <iostream>

#include "layout.h"

DWORD g_lastLayoutTick = 0;

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

  if (!pDwmGetWindowAttribute) {
    return true;
  }

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

void CALLBACK WinEventHookProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                               LONG idObject, LONG idChild, DWORD, DWORD) {
  if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF) return;
  if (event != EVENT_OBJECT_SHOW && event != EVENT_OBJECT_DESTROY) return;

  if (event == EVENT_OBJECT_SHOW) {
    if (!IsWMWindow(hwnd)) return;

    wchar_t title[256] = {0};
    GetWindowTextW(hwnd, title, 256);
    std::wcout << L"Window shown: hwnd=" << hwnd << L", title='" << title
               << L"'" << std::endl;
  }

  DWORD now = GetTickCount();
  if ((now - g_lastLayoutTick) < 120) return;

  g_lastLayoutTick = now;
  RecalculateAndApplyLayout(IsWMWindow);
}

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  HWINEVENTHOOK showHook = SetWinEventHook(
      EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, NULL, WinEventHookProc, 0, 0,
      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  HWINEVENTHOOK destroyHook = SetWinEventHook(
      EVENT_OBJECT_DESTROY, EVENT_OBJECT_DESTROY, NULL, WinEventHookProc, 0, 0,
      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  if (!showHook || !destroyHook) return 1;

  RecalculateAndApplyLayout(IsWMWindow);

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UnhookWinEvent(showHook);
  UnhookWinEvent(destroyHook);
  return 0;
}
