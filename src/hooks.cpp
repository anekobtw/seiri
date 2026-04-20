#include "hooks.h"

#include <iostream>

#include "layout_controller.h"
#include "state.h"
#include "window_manager.h"

namespace {

AppState* g_state = nullptr;

static HWINEVENTHOOK AddHook(DWORD event, DWORD flags) {
  return SetWinEventHook(event, event, nullptr, WinEventHookProc, 0, 0, flags);
}

void HandleWinEvent(AppState& state, DWORD event, HWND hwnd, LONG idObject,
                    LONG idChild) {
  if (!hwnd)
    return;

  if (event == EVENT_SYSTEM_MOVESIZESTART) {
    if (IsWMWindow(hwnd)) {
      state.isMoveSizeActive = true;
      state.moveSizeHwnd = hwnd;
    }
    return;
  }

  if (event == EVENT_SYSTEM_MOVESIZEEND) {
    if (state.moveSizeHwnd == hwnd || state.moveSizeHwnd == nullptr) {
      state.isMoveSizeActive = false;
      state.moveSizeHwnd = nullptr;
    }

    if (IsWMWindow(hwnd) && GetWindowRect(hwnd, &state.anchorRect)) {
      state.anchorHwnd = hwnd;
      state.hasAnchorRect = true;
    }

    QueueRelayout(state);
    return;
  }

  if (event == EVENT_SYSTEM_MINIMIZESTART ||
      event == EVENT_SYSTEM_MINIMIZEEND) {
    if (hwnd == state.anchorHwnd)
      ClearAnchor(state);
    QueueRelayout(state);
    return;
  }

  if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
    return;

  if (event == EVENT_OBJECT_STATECHANGE) {
    const bool managedBefore =
        state.managedWindows.find(hwnd) != state.managedWindows.end();
    const bool managedNow = IsWMWindow(hwnd);
    if (managedNow)
      state.managedWindows.insert(hwnd);
    else
      state.managedWindows.erase(hwnd);

    if (managedBefore || managedNow)
      QueueRelayout(state);
    return;
  }

  if (event == EVENT_OBJECT_SHOW) {
    if (!IsWMWindow(hwnd) || !state.managedWindows.insert(hwnd).second)
      return;

    state.pendingOpenHwnd = hwnd;
    state.hasPendingOpenTargetRect = false;
    state.pendingOpenStage = true;

    wchar_t title[256] = {0};
    GetWindowTextW(hwnd, title, 256);
    std::wcout << L"Window added: hwnd=" << hwnd << L", title='" << title
               << L"'" << std::endl;

    QueueRelayout(state);
    return;
  }

  if (event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_HIDE) {
    if (hwnd == state.anchorHwnd)
      ClearAnchor(state);

    if (hwnd == state.moveSizeHwnd) {
      state.moveSizeHwnd = nullptr;
      state.isMoveSizeActive = false;
    }

    if (hwnd == state.pendingOpenHwnd) {
      state.pendingOpenHwnd = nullptr;
      state.pendingOpenStage = false;
      state.hasPendingOpenTargetRect = false;
      KillTimer(nullptr, kOpenWindowTimerId);
    }

    if (state.managedWindows.erase(hwnd) > 0)
      QueueRelayout(state);
  }
}

} // namespace

bool InstallHooks(DWORD hookFlags, HookSet* out, AppState* state) {
  if (!out || !state)
    return false;

  g_state = state;

  out->show = AddHook(EVENT_OBJECT_SHOW, hookFlags);
  out->hide = AddHook(EVENT_OBJECT_HIDE, hookFlags);
  out->destroy = AddHook(EVENT_OBJECT_DESTROY, hookFlags);
  out->moveSizeStart = AddHook(EVENT_SYSTEM_MOVESIZESTART, hookFlags);
  out->moveSizeEnd = AddHook(EVENT_SYSTEM_MOVESIZEEND, hookFlags);
  out->minimizeStart = AddHook(EVENT_SYSTEM_MINIMIZESTART, hookFlags);
  out->minimizeEnd = AddHook(EVENT_SYSTEM_MINIMIZEEND, hookFlags);
  out->stateChange = AddHook(EVENT_OBJECT_STATECHANGE, hookFlags);

  if (out->show && out->hide && out->destroy && out->moveSizeStart &&
      out->moveSizeEnd && out->minimizeStart && out->minimizeEnd &&
      out->stateChange) {
    return true;
  }

  UnhookAll(*out);
  *out = {};
  g_state = nullptr;
  return false;
}

void UnhookAll(const HookSet& hooks) {
  if (hooks.show)
    UnhookWinEvent(hooks.show);
  if (hooks.hide)
    UnhookWinEvent(hooks.hide);
  if (hooks.destroy)
    UnhookWinEvent(hooks.destroy);
  if (hooks.moveSizeStart)
    UnhookWinEvent(hooks.moveSizeStart);
  if (hooks.moveSizeEnd)
    UnhookWinEvent(hooks.moveSizeEnd);
  if (hooks.minimizeStart)
    UnhookWinEvent(hooks.minimizeStart);
  if (hooks.minimizeEnd)
    UnhookWinEvent(hooks.minimizeEnd);
  if (hooks.stateChange)
    UnhookWinEvent(hooks.stateChange);

  g_state = nullptr;
}

void CALLBACK WinEventHookProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
                               LONG idObject, LONG idChild, DWORD, DWORD) {
  if (!g_state)
    return;
  HandleWinEvent(*g_state, event, hwnd, idObject, idChild);
}
