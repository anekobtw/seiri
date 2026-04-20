#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <unordered_set>

constexpr UINT kRelayoutMessage = WM_APP + 1;
constexpr DWORD kRelayoutDebounceMs = 120;
constexpr int kLayoutAnimationMs = 130;
constexpr int kOpenWindowAnimationMs = 180;
constexpr UINT_PTR kOpenWindowTimerId = 1;

struct AppState {
  UINT superKey = VK_MENU;
  bool isRelayoutQueued = false;
  DWORD lastRelayoutTick = 0;
  bool isMoveSizeActive = false;
  HWND moveSizeHwnd = nullptr;
  std::unordered_set<HWND> managedWindows;
  HWND anchorHwnd = nullptr;
  RECT anchorRect{};
  bool hasAnchorRect = false;
  HWND pendingOpenHwnd = nullptr;
  HWND fullscreenHwnd = nullptr;
  RECT pendingOpenTargetRect{};
  bool hasPendingOpenTargetRect = false;
  bool pendingOpenStage = false;
};

inline void ClearAnchor(AppState &state) {
  state.anchorHwnd = nullptr;
  state.hasAnchorRect = false;
}
