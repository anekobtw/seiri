#pragma once

#include <windows.h>

struct AppState;

struct HookSet {
  HWINEVENTHOOK show = nullptr;
  HWINEVENTHOOK hide = nullptr;
  HWINEVENTHOOK destroy = nullptr;
  HWINEVENTHOOK moveSizeStart = nullptr;
  HWINEVENTHOOK moveSizeEnd = nullptr;
  HWINEVENTHOOK minimizeStart = nullptr;
  HWINEVENTHOOK minimizeEnd = nullptr;
  HWINEVENTHOOK stateChange = nullptr;
};

bool InstallHooks(DWORD hookFlags, HookSet *out, AppState *state);
void UnhookAll(const HookSet &hooks);
void CALLBACK WinEventHookProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
