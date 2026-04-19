#include "layout.h"

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

constexpr int kOuterGap = 10;
constexpr int kInnerGap = 8;
constexpr double kSplitRatio = 0.56;

struct MonitorBucket {
  HMONITOR monitor;
  RECT workArea;
  std::vector<HWND> windows;
};

struct EnumContext {
  std::vector<HWND>* windows;
  WindowFilterFn filter;
};

bool g_isApplyingLayout = false;

int Width(const RECT& r) { return r.right - r.left; }
int Height(const RECT& r) { return r.bottom - r.top; }

RECT InsetRect(const RECT& r, int inset) {
  RECT out = r;
  out.left += inset;
  out.top += inset;
  out.right -= inset;
  out.bottom -= inset;

  if (out.right < out.left) out.right = out.left;
  if (out.bottom < out.top) out.bottom = out.top;

  return out;
}

RECT SafeMonitorWorkArea(HMONITOR monitor) {
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);

  if (monitor && GetMonitorInfo(monitor, &mi)) return mi.rcWork;

  RECT fallback{};
  fallback.left = 0;
  fallback.top = 0;
  fallback.right = GetSystemMetrics(SM_CXSCREEN);
  fallback.bottom = GetSystemMetrics(SM_CYSCREEN);
  return fallback;
}

void PushCell(std::vector<WindowRect>& out, HWND hwnd, const RECT& r) {
  RECT safe = r;
  if (safe.right < safe.left) safe.right = safe.left;
  if (safe.bottom < safe.top) safe.bottom = safe.top;

  out.push_back({hwnd, safe});
}

void SplitVertical(const RECT& in, RECT& left, RECT& right) {
  int totalW = Width(in);
  if (totalW <= 1) {
    left = in;
    right = in;
    return;
  }

  int usable = std::max(1, totalW - kInnerGap);
  int leftW = static_cast<int>(usable * kSplitRatio);
  int minW = std::max(80, usable / 4);
  if (leftW < minW) leftW = minW;
  if (leftW > usable - minW) leftW = usable - minW;

  left = in;
  left.right = left.left + leftW;

  right = in;
  right.left = left.right + kInnerGap;
}

void SplitHorizontal(const RECT& in, RECT& top, RECT& bottom) {
  int totalH = Height(in);
  if (totalH <= 1) {
    top = in;
    bottom = in;
    return;
  }

  int usable = std::max(1, totalH - kInnerGap);
  int topH = static_cast<int>(usable * kSplitRatio);
  int minH = std::max(70, usable / 4);
  if (topH < minH) topH = minH;
  if (topH > usable - minH) topH = usable - minH;

  top = in;
  top.bottom = top.top + topH;

  bottom = in;
  bottom.top = top.bottom + kInnerGap;
}

void LayoutDwindle(std::vector<WindowRect>& out,
                   const std::vector<HWND>& windows,
                   const RECT& monitorWorkArea) {
  if (windows.empty()) return;

  RECT remaining = InsetRect(monitorWorkArea, kOuterGap);
  int n = static_cast<int>(windows.size());

  if (n == 1) {
    PushCell(out, windows[0], remaining);
    return;
  }

  // Hyprland dwindle-like recursive split (spiral-ish alternating orientation).
  // Exact 1:1 is compositor/tree-state dependent, but this mirrors behavior
  // closely.
  bool splitVertical = Width(remaining) >= Height(remaining);

  for (int i = 0; i < n - 1; ++i) {
    RECT a{}, b{};

    if (splitVertical) {
      SplitVertical(remaining, a, b);
    } else {
      SplitHorizontal(remaining, a, b);
    }

    PushCell(out, windows[i], a);
    remaining = b;
    splitVertical = !splitVertical;
  }

  PushCell(out, windows[n - 1], remaining);
}

BOOL CALLBACK CollectWindowsProc(HWND hwnd, LPARAM lParam) {
  auto* context = reinterpret_cast<EnumContext*>(lParam);
  if (!context || !context->windows || !context->filter) return TRUE;

  if (context->filter(hwnd)) context->windows->push_back(hwnd);

  return TRUE;
}

}  // namespace

std::vector<WindowRect> calculateWindowResolution(
    const std::vector<HWND>& windows) {
  std::vector<WindowRect> result;
  if (windows.empty()) return result;

  std::vector<MonitorBucket> buckets;
  buckets.reserve(4);

  for (HWND hwnd : windows) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    auto it = std::find_if(
        buckets.begin(), buckets.end(),
        [mon](const MonitorBucket& b) { return b.monitor == mon; });

    if (it == buckets.end()) {
      MonitorBucket bucket{};
      bucket.monitor = mon;
      bucket.workArea = SafeMonitorWorkArea(mon);
      bucket.windows.push_back(hwnd);
      buckets.push_back(std::move(bucket));
    } else {
      it->windows.push_back(hwnd);
    }
  }

  HWND foreground = GetForegroundWindow();

  for (auto& bucket : buckets) {
    auto it =
        std::find(bucket.windows.begin(), bucket.windows.end(), foreground);
    if (it != bucket.windows.end() && it != bucket.windows.begin()) {
      std::rotate(bucket.windows.begin(), it, it + 1);
    }

    LayoutDwindle(result, bucket.windows, bucket.workArea);
  }

  return result;
}

void RecalculateAndApplyLayout(WindowFilterFn filter) {
  if (!filter || g_isApplyingLayout) return;

  g_isApplyingLayout = true;

  std::vector<HWND> windows;
  EnumContext context{&windows, filter};
  EnumWindows(CollectWindowsProc, reinterpret_cast<LPARAM>(&context));

  auto layout = calculateWindowResolution(windows);

  int moved = 0;
  for (const auto& item : layout) {
    if (!IsWindow(item.hwnd)) continue;

    RECT current{};
    if (!GetWindowRect(item.hwnd, &current)) continue;

    if (EqualRect(&current, &item.rect)) continue;

    int width = item.rect.right - item.rect.left;
    int height = item.rect.bottom - item.rect.top;

    if (SetWindowPos(item.hwnd, nullptr, item.rect.left, item.rect.top, width,
                     height,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING)) {
      ++moved;
    }
  }

  std::wcout << L"Layout computed for " << layout.size()
             << L" window(s), moved " << moved << L"." << std::endl;

  g_isApplyingLayout = false;
}
