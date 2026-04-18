#include "layout.h"
#include <iostream>
#include <vector>
#include <windows.h>

bool IsWindowManagable(HWND hwnd) {
  LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

  if ((style & WS_THICKFRAME) == 0)
    return false;

  if (exStyle & WS_EX_TOOLWINDOW)
    return false;

  if (exStyle & WS_EX_NOACTIVATE)
    return false;

  return true;
}

bool IsWMWindow(HWND hwnd) {
  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd))
    return false;

  if (GetAncestor(hwnd, GA_ROOT) != hwnd)
    return false;

  if (GetWindow(hwnd, GW_OWNER) != NULL)
    return false;

  if (IsIconic(hwnd) || IsZoomed(hwnd))
    return false;

  if (!IsWindowManagable(hwnd))
    return false;

  return true;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
  auto *windows = reinterpret_cast<std::vector<HWND> *>(lParam);
  if (IsWMWindow(hwnd))
    windows->push_back(hwnd);
  return TRUE;
}

void RecalculateAndApplyLayout() {
  std::vector<HWND> windows;
  EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&windows));

  auto layout = calculateWindowResolution(windows);
  int moved = 0;

  for (const auto &item : layout) {
    RECT current{};
    if (!GetWindowRect(item.hwnd, &current))
      continue;

    if (current.left == item.rect.left && current.top == item.rect.top &&
        current.right == item.rect.right && current.bottom == item.rect.bottom) {
      continue;
    }

    int width = item.rect.right - item.rect.left;
    int height = item.rect.bottom - item.rect.top;

    if (SetWindowPos(item.hwnd, nullptr, item.rect.left, item.rect.top, width,
                     height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING)) {
      ++moved;
    }
  }

  std::wcout << L"Layout computed for " << layout.size() << L" window(s), moved "
             << moved << L"." << std::endl;
}

void CALLBACK WinEventHookProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                               LONG idObject, LONG idChild, DWORD, DWORD) {
  if (event != EVENT_OBJECT_SHOW)
    return;

  if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
    return;

  if (!IsWMWindow(hwnd))
    return;

  wchar_t title[256] = {0};
  GetWindowTextW(hwnd, title, 256);

  std::wcout << L"Window shown: hwnd=" << hwnd << L", title='" << title << L"'"
             << std::endl;

  RecalculateAndApplyLayout();
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
