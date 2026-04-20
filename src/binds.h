#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

struct AppState;

using BindsWindowFilterFn = bool (*)(HWND);

bool InstallBindsHook(BindsWindowFilterFn filter, UINT superKey, AppState *state);
void UninstallBindsHook();
