#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

using AltMaxMinWindowFilterFn = bool (*)(HWND);

bool InstallAltMaxMinHook(AltMaxMinWindowFilterFn filter);
void UninstallAltMaxMinHook();
