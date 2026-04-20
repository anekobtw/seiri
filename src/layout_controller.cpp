#include "layout_controller.h"

#include <unordered_set>

#include "layout.h"
#include "window_manager.h"

namespace {

void ValidateAnchor(AppState &state) {
  if (!state.hasAnchorRect)
    return;

  if (!state.anchorHwnd || !IsWindow(state.anchorHwnd) || !IsWMWindow(state.anchorHwnd)) {
    ClearAnchor(state);
  }
}

HWND GetAnchorWindow(const AppState &state) {
  return state.hasAnchorRect ? state.anchorHwnd : nullptr;
}

const RECT *GetAnchorRect(const AppState &state) {
  return state.hasAnchorRect ? &state.anchorRect : nullptr;
}

bool TryHandlePendingOpen(AppState &state, HWND anchorWindow, const RECT *anchorRect) {
  if (!(state.pendingOpenStage && state.pendingOpenHwnd && state.managedWindows.count(state.pendingOpenHwnd))) {
    return false;
  }

  if (state.fullscreenHwnd)
    return false;

  RECT target{};
  RecalculateAndApplyLayout(IsWMWindow, anchorWindow, anchorRect, kLayoutAnimationMs, state.pendingOpenHwnd, &target, state.fullscreenHwnd);

  state.pendingOpenTargetRect = target;
  state.hasPendingOpenTargetRect = true;
  state.pendingOpenStage = false;

  if (state.hasAnchorRect)
    ClearAnchor(state);

  SetTimer(nullptr, kOpenWindowTimerId, kLayoutAnimationMs, nullptr);
  return true;
}

} // namespace

void QueueRelayout(AppState &state) {
  if (state.isRelayoutQueued)
    return;

  state.isRelayoutQueued = true;
  if (!PostThreadMessage(GetCurrentThreadId(), kRelayoutMessage, 0, 0))
    state.isRelayoutQueued = false;
}

bool HandleRelayoutMessage(AppState &state) {
  state.isRelayoutQueued = false;

  if (state.isMoveSizeActive)
    return true;

  const DWORD now = GetTickCount();
  if ((now - state.lastRelayoutTick) < kRelayoutDebounceMs)
    return true;

  state.lastRelayoutTick = now;
  ApplyLayout(state);
  return true;
}

void ApplyLayout(AppState &state) {
  ValidateAnchor(state);

  std::unordered_set<HWND> next;
  CollectManagedWindows(next);
  state.managedWindows.swap(next);

  if (state.fullscreenHwnd && state.managedWindows.find(state.fullscreenHwnd) == state.managedWindows.end()) {
    state.fullscreenHwnd = nullptr;
  }

  const HWND anchorWindow = GetAnchorWindow(state);
  const RECT *anchorRect = GetAnchorRect(state);

  if (TryHandlePendingOpen(state, anchorWindow, anchorRect))
    return;

  RecalculateAndApplyLayout(IsWMWindow, anchorWindow, anchorRect, kLayoutAnimationMs, nullptr, nullptr, state.fullscreenHwnd);

  if (state.hasAnchorRect)
    ClearAnchor(state);
}
