#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

using AltResizeWindowFilterFn = bool (*)(HWND);
using AltResizeCommitFn = void (*)(HWND, const RECT&);

bool InstallAltResizeHook(AltResizeWindowFilterFn filter,
                          AltResizeCommitFn onCommit);
void UninstallAltResizeHook();
