#include <windows.h>

#include "alt-maxmin.h"
#include "animations.h"
#include "hooks.h"
#include "layout_controller.h"
#include "state.h"
#include "window_manager.h"

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
  MSG initMsg;
  PeekMessage(&initMsg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

  AppState appState;
  CollectManagedWindows(appState.managedWindows);

  HookSet hooks{};
  if (!InstallHooks(WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS, &hooks,
                    &appState))
    return 1;

  if (!InstallAltMaxMinHook(IsWMWindow)) {
    UnhookAll(hooks);
    return 1;
  }

  QueueRelayout(appState);

  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0)) {
    if (msg.message == kRelayoutMessage) {
      HandleRelayoutMessage(appState);
      continue;
    }

    if (HandleAnimationTimerMessage(appState, msg))
      continue;

    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }

  UninstallAltMaxMinHook();
  UnhookAll(hooks);
  return 0;
}