#include <windows.h>
#include <winuser.h>

void CALLBACK WinEvenHook(HWINEVENTHOOK hWinEventHook, DWORD event, HWND hwnd,
                          LONG idObject, LONG idChild, DWORD idEventThread,
                          DWORD dwmsEventTime) {

  if (event != EVENT_OBJECT_SHOW)
    return;
  if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
    return;
  if (!IsWindowVisible(hwnd))
    return;
  if (GetAncestor(hwnd, GA_ROOT) != hwnd)
    return;
  if (GetWindow(hwnd, GW_OWNER) != NULL)
    return;
  if (!WS_EX_TOOLWINDOW)
    return;

  MessageBoxA(NULL, "yesqaqa", "yes", MB_OK);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nShowCmd) {
  MessageBoxA(nullptr, "Hey!", "caption", MB_OK);

  HWINEVENTHOOK hook =
      SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_SHOW, NULL, WinEvenHook,
                      0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);

  if (!hook) {
    return 1;
  }

  MSG msg;
  while (GetMessage(&msg, NULL, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  return 0;
}
