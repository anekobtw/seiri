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
void RecalculateAndApplyLayout(WindowFilterFn filter);
