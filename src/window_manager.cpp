#include "window_manager.h"

#include <unordered_set>

bool IsWMWindow(HWND hwnd) {
  constexpr DWORD kDwAttributeCloaked = 14;

  if (!hwnd || !IsWindow(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd))
    return false;
  if (GetAncestor(hwnd, GA_ROOT) != hwnd || GetWindow(hwnd, GW_OWNER))
    return false;

  const LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
  const LONG_PTR exStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
  if (!(style & WS_THICKFRAME) || (exStyle & WS_EX_TOOLWINDOW) || (exStyle & WS_EX_NOACTIVATE))
    return false;

  using DwmGetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, PVOID, DWORD);
  static DwmGetWindowAttributeFn getWindowAttribute = nullptr;
  static bool initialized = false;

  if (!initialized) {
    if (HMODULE dwm = LoadLibraryW(L"dwmapi.dll")) {
      getWindowAttribute = reinterpret_cast<DwmGetWindowAttributeFn>(
          GetProcAddress(dwm, "DwmGetWindowAttribute"));
    }
    initialized = true;
  }

  if (!getWindowAttribute)
    return true;

  DWORD cloaked = 0;
  const HRESULT hr = getWindowAttribute(hwnd, kDwAttributeCloaked, &cloaked, sizeof(cloaked));

  return FAILED(hr) || cloaked == 0;
}

void CollectManagedWindows(std::unordered_set<HWND> &out) {
  EnumWindows(
      [](HWND hwnd, LPARAM lParam) -> BOOL {
        auto *set = reinterpret_cast<std::unordered_set<HWND> *>(lParam);
        if (IsWMWindow(hwnd))
          set->insert(hwnd);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&out));
}
