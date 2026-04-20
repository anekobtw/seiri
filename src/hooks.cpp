#include "hooks.h"

#include <iostream>

#include "layout_controller.h"
#include "state.h"
#include "window_manager.h"

namespace {

AppState *g_state = nullptr;

static HWINEVENTHOOK AddHook(DWORD event, DWORD flags) {
  return SetWinEventHook(event, event, nullptr, WinEventHookProc, 0, 0, flags);
}

void HandleWinEvent(AppState &state, DWORD event, HWND hwnd, LONG idObject, LONG idChild) {
  if (!hwnd)
    return;

  // Move/resize tracking
  if (event == EVENT_SYSTEM_MOVESIZESTART) {
    if (IsWMWindow(hwnd))
      state.isMoveSizeActive = true, state.moveSizeHwnd = hwnd;
    return;
  }

  if (event == EVENT_SYSTEM_MOVESIZEEND) {
    state.isMoveSizeActive = false;
    if (IsWMWindow(hwnd) && GetWindowRect(hwnd, &state.anchorRect))
      state.anchorHwnd = hwnd, state.hasAnchorRect = true;
    state.moveSizeHwnd = nullptr;
    QueueRelayout(state);
    return;
  }

  // Minimize handling
  if (event == EVENT_SYSTEM_MINIMIZESTART || event == EVENT_SYSTEM_MINIMIZEEND) {
    if (hwnd == state.anchorHwnd)
      ClearAnchor(state);
    QueueRelayout(state);
    return;
  }

  // Only process object events for the window itself
  if (idObject != OBJID_WINDOW || idChild != CHILDID_SELF)
    return;

  // State change (managed window gained/lost management)
  if (event == EVENT_OBJECT_STATECHANGE) {
    const bool wasManagedBefore = state.managedWindows.count(hwnd) > 0;
    const bool isManagedNow = IsWMWindow(hwnd);

    if (isManagedNow)
      state.managedWindows.insert(hwnd);
    else
      state.managedWindows.erase(hwnd);

    if (wasManagedBefore || isManagedNow)
      QueueRelayout(state);
    return;
  }

  // Window show
  if (event == EVENT_OBJECT_SHOW) {
    if (!IsWMWindow(hwnd))
      return;
    if (!state.managedWindows.insert(hwnd).second)
      return;

    state.pendingOpenHwnd = hwnd;
    state.pendingOpenStage = true;
    state.hasPendingOpenTargetRect = false;

    QueueRelayout(state);
    return;
  }

  // Window hide/destroy
  if (event == EVENT_OBJECT_DESTROY || event == EVENT_OBJECT_HIDE) {
    if (hwnd == state.anchorHwnd)
      ClearAnchor(state);
    if (hwnd == state.moveSizeHwnd)
      state.moveSizeHwnd = nullptr, state.isMoveSizeActive = false;
    if (hwnd == state.pendingOpenHwnd) {
      state.pendingOpenHwnd = nullptr;
      state.pendingOpenStage = false;
      state.hasPendingOpenTargetRect = false;
      KillTimer(nullptr, kOpenWindowTimerId);
    }
    if (hwnd == state.fullscreenHwnd)
      state.fullscreenHwnd = nullptr;

    if (state.managedWindows.erase(hwnd) > 0)
      QueueRelayout(state);
  }
}

} // namespace

bool InstallHooks(DWORD hookFlags, HookSet *out, AppState *state) {
  if (!out || !state)
    return false;

  g_state = state;

  // Install all hooks
  const DWORD events[] = {
      EVENT_OBJECT_SHOW, EVENT_OBJECT_HIDE, EVENT_OBJECT_DESTROY,
      EVENT_SYSTEM_MOVESIZESTART, EVENT_SYSTEM_MOVESIZEEND,
      EVENT_SYSTEM_MINIMIZESTART, EVENT_SYSTEM_MINIMIZEEND,
      EVENT_OBJECT_STATECHANGE};

  HWINEVENTHOOK *hooks[] = {
      &out->show, &out->hide, &out->destroy,
      &out->moveSizeStart, &out->moveSizeEnd,
      &out->minimizeStart, &out->minimizeEnd,
      &out->stateChange};

  for (size_t i = 0; i < 8; ++i)
    *hooks[i] = AddHook(events[i], hookFlags);

  // Check if all succeeded
  for (size_t i = 0; i < 8; ++i) {
    if (!*hooks[i]) {
      UnhookAll(*out);
      *out = {};
      g_state = nullptr;
      return false;
    }
  }

  return true;
}

void UnhookAll(const HookSet &hooks) {
  const HWINEVENTHOOK *hookPtrs[] = {
      &hooks.show, &hooks.hide, &hooks.destroy,
      &hooks.moveSizeStart, &hooks.moveSizeEnd,
      &hooks.minimizeStart, &hooks.minimizeEnd,
      &hooks.stateChange};

  for (const auto *hookPtr : hookPtrs) {
    if (*hookPtr)
      UnhookWinEvent(*hookPtr);
  }

  g_state = nullptr;
}

void CALLBACK WinEventHookProc(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD, DWORD) {
  if (!g_state)
    return;
  HandleWinEvent(*g_state, event, hwnd, idObject, idChild);
}
