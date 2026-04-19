#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <vector>

struct WindowRect {
  HWND hwnd;
  RECT rect;
};

using WindowFilterFn = bool (*)(HWND);

std::vector<WindowRect> calculateWindowResolution(
    const std::vector<HWND>& windows);
std::vector<WindowRect> calculateWindowResolutionWithAnchor(
    const std::vector<HWND>& windows,
    HWND anchorWindow,
    const RECT* anchorRect);

void RecalculateAndApplyLayout(WindowFilterFn filter,
                               HWND anchorWindow = nullptr,
                               const RECT* anchorRect = nullptr);
