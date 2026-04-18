#include <iostream>
#include <windows.h>
#include <winuser.h>

bool IsRealTopLevelAppWindow(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd))
    return false;

  if (GetAncestor(hwnd, GA_ROOT) != hwnd)
    return false;

  if (GetWindow(hwnd, GW_OWNER) != NULL)
    return false;

  LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

  if (exStyle & WS_EX_TOOLWINDOW)
    return false;

  if (exStyle & WS_EX_NOACTIVATE)
    return false;

  return true;
}

void CALLBACK WinEventHookProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                               LONG idObject, LONG idChild, DWORD, DWORD) {
  if (event != EVENT_OBJECT_SHOW)
    return;

  if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
    return;

  if (!IsRealTopLevelAppWindow(hwnd))
    return;

  wchar_t title[256] = {0};
  GetWindowTextW(hwnd, title, 256);

  std::wcout << L"Window shown: hwnd=" << hwnd << L", title='" << title << L"'"
             << std::endl;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd) {

  HWINEVENTHOOK hook = SetWinEventHook(
      EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, NULL, WinEventHookProc, 0, 0,
      WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  if (!hook) {
    return 1;
  }

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UnhookWinEvent(hook);
  return 0;
}
