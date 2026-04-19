#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

void AnimateWindowTransform(HWND hwnd, int x, int y, int width, int height,
                            int durationMs);
void AnimateWindowMove(HWND hwnd, int x, int y, int durationMs);
void AnimateWindowResize(HWND hwnd, int width, int height, int durationMs);

#ifdef __cplusplus
}
#endif
