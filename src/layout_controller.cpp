#include "layout_controller.h"

#include <unordered_set>

#include "layout.h"
#include "window_manager.h"

void QueueRelayout(AppState& state) {
  if (state.isRelayoutQueued)
    return;
  state.isRelayoutQueued = true;
  if (!PostThreadMessage(GetCurrentThreadId(), kRelayoutMessage, 0, 0))
    state.isRelayoutQueued = false;
}

bool HandleRelayoutMessage(AppState& state) {
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

void ApplyLayout(AppState& state) {
  if (state.hasAnchorRect && (!state.anchorHwnd || !IsWindow(state.anchorHwnd) || !IsWMWindow(state.anchorHwnd)))
    ClearAnchor(state);

  // Update managed windows
  std::unordered_set<HWND> next;
  CollectManagedWindows(next);
  state.managedWindows.swap(next);

  // Handle pending window
  if (state.pendingOpenStage && state.pendingOpenHwnd && state.managedWindows.count(state.pendingOpenHwnd)) {
    RECT target{};
    RecalculateAndApplyLayout(IsWMWindow,
                              state.hasAnchorRect ? state.anchorHwnd : nullptr,
                              state.hasAnchorRect ? &state.anchorRect : nullptr,
                              kLayoutAnimationMs, state.pendingOpenHwnd, &target);
    state.pendingOpenTargetRect = target;
    state.hasPendingOpenTargetRect = true;
    state.pendingOpenStage = false;
    if (state.hasAnchorRect)
      ClearAnchor(state);
    SetTimer(nullptr, kOpenWindowTimerId, kLayoutAnimationMs, nullptr);
    return;
  }

  // Regular layout
  RecalculateAndApplyLayout(IsWMWindow,
                            state.hasAnchorRect ? state.anchorHwnd : nullptr,
                            state.hasAnchorRect ? &state.anchorRect : nullptr,
                            kLayoutAnimationMs);
  if (state.hasAnchorRect)
    ClearAnchor(state);
}
