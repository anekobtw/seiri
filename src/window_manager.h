#pragma once

#include <windows.h>

#include <unordered_set>

bool IsWMWindow(HWND hwnd);
void CollectManagedWindows(std::unordered_set<HWND>& out);
