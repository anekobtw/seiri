#include "layout.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

constexpr int kOuterGap = 10;
constexpr int kInnerGap = 8;
constexpr double kMasterRatio = 0.60;

struct MonitorBucket {
  HMONITOR monitor;
  RECT workArea;
  std::vector<HWND> windows;
};

int Width(const RECT &r) { return r.right - r.left; }
int Height(const RECT &r) { return r.bottom - r.top; }

RECT InsetRect(const RECT &r, int inset) {
  RECT out = r;
  out.left += inset;
  out.top += inset;
  out.right -= inset;
  out.bottom -= inset;

  if (out.right < out.left)
    out.right = out.left;
  if (out.bottom < out.top)
    out.bottom = out.top;

  return out;
}

RECT SafeMonitorWorkArea(HMONITOR monitor) {
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);

  if (monitor && GetMonitorInfo(monitor, &mi))
    return mi.rcWork;

  RECT fallback{};
  fallback.left = 0;
  fallback.top = 0;
  fallback.right = GetSystemMetrics(SM_CXSCREEN);
  fallback.bottom = GetSystemMetrics(SM_CYSCREEN);
  return fallback;
}

void PushCell(std::vector<WindowRect> &out, HWND hwnd, int left, int top, int right,
              int bottom) {
  RECT r{};
  r.left = left;
  r.top = top;
  r.right = std::max(left, right);
  r.bottom = std::max(top, bottom);
  out.push_back({hwnd, r});
}

void LayoutGrid(std::vector<WindowRect> &out, const std::vector<HWND> &windows,
                size_t start, size_t count, const RECT &area) {
  if (count == 0)
    return;

  int cols = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
  int rows = static_cast<int>((count + cols - 1) / cols);

  int totalW = Width(area);
  int totalH = Height(area);
  if (totalW <= 0 || totalH <= 0)
    return;

  int usableW = std::max(1, totalW - (cols - 1) * kInnerGap);
  int usableH = std::max(1, totalH - (rows - 1) * kInnerGap);

  int baseCellW = usableW / cols;
  int extraW = usableW % cols;
  int baseCellH = usableH / rows;
  int extraH = usableH % rows;

  size_t idx = start;
  int y = area.top;

  for (int row = 0; row < rows && idx < start + count; ++row) {
    int thisRowH = baseCellH + (row < extraH ? 1 : 0);

    int x = area.left;
    for (int col = 0; col < cols && idx < start + count; ++col) {
      int thisColW = baseCellW + (col < extraW ? 1 : 0);
      PushCell(out, windows[idx], x, y, x + thisColW, y + thisRowH);

      x += thisColW + kInnerGap;
      ++idx;
    }

    y += thisRowH + kInnerGap;
  }
}

void LayoutMonitorWindows(std::vector<WindowRect> &out,
                          const std::vector<HWND> &windows,
                          const RECT &monitorWorkArea) {
  if (windows.empty())
    return;

  RECT area = InsetRect(monitorWorkArea, kOuterGap);
  int n = static_cast<int>(windows.size());

  // 1 window: fill work area.
  if (n == 1) {
    PushCell(out, windows[0], area.left, area.top, area.right, area.bottom);
    return;
  }

  // 2 windows: simple side-by-side split.
  if (n == 2) {
    int totalW = Width(area);
    int leftW = (totalW - kInnerGap) / 2;

    PushCell(out, windows[0], area.left, area.top, area.left + leftW, area.bottom);
    PushCell(out, windows[1], area.left + leftW + kInnerGap, area.top, area.right,
             area.bottom);
    return;
  }

  // 3-6 windows: master + stack for a more usable focus layout.
  if (n <= 6) {
    int totalW = Width(area);
    int masterW = static_cast<int>(std::round((totalW - kInnerGap) * kMasterRatio));
    int minMaster = 200;
    int maxMaster = totalW - 200;
    if (maxMaster < minMaster)
      maxMaster = minMaster;
    if (masterW < minMaster)
      masterW = minMaster;
    if (masterW > maxMaster)
      masterW = maxMaster;

    RECT master{};
    master.left = area.left;
    master.top = area.top;
    master.right = area.left + masterW;
    master.bottom = area.bottom;

    RECT stack{};
    stack.left = master.right + kInnerGap;
    stack.top = area.top;
    stack.right = area.right;
    stack.bottom = area.bottom;

    PushCell(out, windows[0], master.left, master.top, master.right, master.bottom);
    LayoutGrid(out, windows, 1, windows.size() - 1, stack);
    return;
  }

  // 7+ windows: full grid is clearer than tiny stack tiles.
  LayoutGrid(out, windows, 0, windows.size(), area);
}

} // namespace

std::vector<WindowRect>
calculateWindowResolution(const std::vector<HWND> &windows) {
  std::vector<WindowRect> result;
  if (windows.empty())
    return result;

  std::vector<MonitorBucket> buckets;
  buckets.reserve(4);

  // Group windows per monitor.
  for (HWND hwnd : windows) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    auto it = std::find_if(buckets.begin(), buckets.end(),
                           [mon](const MonitorBucket &b) {
                             return b.monitor == mon;
                           });

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

  // Prioritize active window in each monitor bucket (becomes master in master-stack).
  for (auto &bucket : buckets) {
    auto it = std::find(bucket.windows.begin(), bucket.windows.end(), foreground);
    if (it != bucket.windows.end() && it != bucket.windows.begin()) {
      std::rotate(bucket.windows.begin(), it, it + 1);
    }

    LayoutMonitorWindows(result, bucket.windows, bucket.workArea);
  }

  return result;
}
