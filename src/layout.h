#pragma once

#include <vector>
#include <windows.h>

struct WindowRect {
  HWND hwnd;
  RECT rect;
};

std::vector<WindowRect> calculateWindowResolution(const std::vector<HWND>& windows);
