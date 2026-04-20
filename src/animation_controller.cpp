#include "animation_controller.h"
#include "animations.h"
#include "window_manager.h"
#include <algorithm>

bool HandleAnimationTimerMessage(AppState &state, const MSG &msg) {
  if (msg.message != WM_TIMER || msg.wParam != kOpenWindowTimerId)
    return false;

  KillTimer(nullptr, kOpenWindowTimerId);

  if (!state.pendingOpenHwnd || !state.hasPendingOpenTargetRect || !IsWindow(state.pendingOpenHwnd) || !IsWMWindow(state.pendingOpenHwnd)) {
    state.pendingOpenHwnd = nullptr, state.hasPendingOpenTargetRect = false;
    return true;
  }

  int targetW = state.pendingOpenTargetRect.right - state.pendingOpenTargetRect.left;
  int targetH = state.pendingOpenTargetRect.bottom - state.pendingOpenTargetRect.top;
  int startW = std::max(120, (targetW * 65) / 100);
  int startH = std::max(90, (targetH * 65) / 100);
  int centerX = state.pendingOpenTargetRect.left + targetW / 2;
  int centerY = state.pendingOpenTargetRect.top + targetH / 2;
  int startX = centerX - startW / 2;
  int startY = centerY - startH / 2;

  SetWindowPos(state.pendingOpenHwnd, nullptr, startX, startY, startW, startH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
  AnimateWindowTransform(state.pendingOpenHwnd, state.pendingOpenTargetRect.left, state.pendingOpenTargetRect.top, targetW, targetH, kOpenWindowAnimationMs);

  state.pendingOpenHwnd = nullptr, state.hasPendingOpenTargetRect = false;
  return true;
}
