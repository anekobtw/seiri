#include "layout.h"

#include <algorithm>
#include <iostream>
#include <vector>

namespace {

constexpr int kOuterGap = 10;
constexpr int kInnerGap = 8;
constexpr double kSplitRatio = 0.56;
constexpr int kMinTileWidth = 80;
constexpr int kMinTileHeight = 70;
constexpr int kMinLeftoverArea = 20000;

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
int Area(const RECT& r) {
  int w = Width(r);
  int h = Height(r);
  if (w <= 0 || h <= 0) return 0;
  return w * h;
}

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
  int minW = std::max(kMinTileWidth, usable / 4);
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
  int minH = std::max(kMinTileHeight, usable / 4);
  if (topH < minH) topH = minH;
  if (topH > usable - minH) topH = usable - minH;

  top = in;
  top.bottom = top.top + topH;

  bottom = in;
  bottom.top = top.bottom + kInnerGap;
}

void LayoutDwindleInArea(std::vector<WindowRect>& out,
                         const std::vector<HWND>& windows, const RECT& area) {
  if (windows.empty()) return;

  RECT remaining = area;
  int n = static_cast<int>(windows.size());

  if (n == 1) {
    PushCell(out, windows[0], remaining);
    return;
  }

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

RECT ClampRectToArea(const RECT& r, const RECT& area) {
  RECT out = r;

  int w = Width(out);
  int h = Height(out);
  if (w < kMinTileWidth) w = kMinTileWidth;
  if (h < kMinTileHeight) h = kMinTileHeight;

  if (w > Width(area)) w = Width(area);
  if (h > Height(area)) h = Height(area);

  if (out.left < area.left) out.left = area.left;
  if (out.top < area.top) out.top = area.top;

  out.right = out.left + w;
  out.bottom = out.top + h;

  if (out.right > area.right) {
    out.right = area.right;
    out.left = out.right - w;
  }

  if (out.bottom > area.bottom) {
    out.bottom = area.bottom;
    out.top = out.bottom - h;
  }

  return out;
}

RECT LargestRegionAroundAnchor(const RECT& workArea, const RECT& anchor) {
  RECT candidates[4] = {
      {workArea.left, workArea.top, anchor.left - kInnerGap, workArea.bottom},
      {anchor.right + kInnerGap, workArea.top, workArea.right, workArea.bottom},
      {workArea.left, workArea.top, workArea.right, anchor.top - kInnerGap},
      {workArea.left, anchor.bottom + kInnerGap, workArea.right,
       workArea.bottom},
  };

  int bestIdx = -1;
  int bestArea = -1;
  for (int i = 0; i < 4; ++i) {
    if (candidates[i].right < candidates[i].left)
      candidates[i].right = candidates[i].left;
    if (candidates[i].bottom < candidates[i].top)
      candidates[i].bottom = candidates[i].top;

    int a = Area(candidates[i]);
    if (a > bestArea) {
      bestArea = a;
      bestIdx = i;
    }
  }

  if (bestIdx < 0) return workArea;
  return candidates[bestIdx];
}

void BuildMonitorBuckets(const std::vector<HWND>& windows,
                         std::vector<MonitorBucket>& buckets) {
  buckets.clear();
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
  return calculateWindowResolutionWithAnchor(windows, nullptr, nullptr);
}

std::vector<WindowRect> calculateWindowResolutionWithAnchor(
    const std::vector<HWND>& windows, HWND anchorHwnd, const RECT* anchorRect) {
  std::vector<WindowRect> result;
  if (windows.empty()) return result;

  std::vector<MonitorBucket> buckets;
  BuildMonitorBuckets(windows, buckets);

  HWND foreground = GetForegroundWindow();
  const bool hasAnchor = anchorHwnd && anchorRect && IsWindow(anchorHwnd);

  for (auto& bucket : buckets) {
    auto focusIt =
        std::find(bucket.windows.begin(), bucket.windows.end(), foreground);
    if (focusIt != bucket.windows.end() && focusIt != bucket.windows.begin()) {
      std::rotate(bucket.windows.begin(), focusIt, focusIt + 1);
    }

    RECT tiledArea = InsetRect(bucket.workArea, kOuterGap);

    if (!hasAnchor) {
      LayoutDwindleInArea(result, bucket.windows, tiledArea);
      continue;
    }

    auto anchorIt =
        std::find(bucket.windows.begin(), bucket.windows.end(), anchorHwnd);
    if (anchorIt == bucket.windows.end()) {
      LayoutDwindleInArea(result, bucket.windows, tiledArea);
      continue;
    }

    RECT fixedAnchor = ClampRectToArea(*anchorRect, tiledArea);
    PushCell(result, anchorHwnd, fixedAnchor);

    std::vector<HWND> others;
    others.reserve(bucket.windows.size());
    for (HWND w : bucket.windows) {
      if (w != anchorHwnd) others.push_back(w);
    }

    if (others.empty()) continue;

    RECT leftover = LargestRegionAroundAnchor(tiledArea, fixedAnchor);
    if (Area(leftover) < kMinLeftoverArea) {
      continue;
    }

    LayoutDwindleInArea(result, others, leftover);
  }

  return result;
}

void RecalculateAndApplyLayout(WindowFilterFn filter, HWND anchorHwnd,
                               const RECT* anchorRect) {
  if (!filter || g_isApplyingLayout) return;

  g_isApplyingLayout = true;

  std::vector<HWND> windows;
  EnumContext context{&windows, filter};
  EnumWindows(CollectWindowsProc, reinterpret_cast<LPARAM>(&context));

  auto layout =
      calculateWindowResolutionWithAnchor(windows, anchorHwnd, anchorRect);

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
