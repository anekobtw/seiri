#include "layout.h"

#include <algorithm>
#include <vector>

#include "animations.h"

namespace {

constexpr int kOuterGap = 10;
constexpr int kInnerGap = 8;
constexpr double kSplitRatio = 0.56;
constexpr int kMinTileWidth = 80;
constexpr int kMinTileHeight = 70;
constexpr int kMinLeftoverArea = 20000;

struct MonitorBucket {
  HMONITOR monitor = nullptr;
  RECT workArea{};
  std::vector<HWND> windows;
};

struct EnumContext {
  std::vector<HWND> *windows = nullptr;
  WindowFilterFn filter = nullptr;
};

bool g_isApplyingLayout = false;

inline int Width(const RECT &rect) { return rect.right - rect.left; }
inline int Height(const RECT &rect) { return rect.bottom - rect.top; }
inline int Area(const RECT &rect) {
  const int w = Width(rect), h = Height(rect);
  return (w > 0 && h > 0) ? w * h : 0;
}

RECT InsetRect(const RECT &rect, int inset) {
  RECT out{rect.left + inset, rect.top + inset, rect.right - inset,
           rect.bottom - inset};
  if (out.right < out.left)
    out.right = out.left;
  if (out.bottom < out.top)
    out.bottom = out.top;
  return out;
}

RECT SafeMonitorWorkArea(HMONITOR monitor) {
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (monitor && GetMonitorInfo(monitor, &info))
    return info.rcWork;
  return {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

void PushCell(std::vector<WindowRect> &out, HWND hwnd, const RECT &rect) {
  RECT safe = rect;
  if (safe.right < safe.left)
    safe.right = safe.left;
  if (safe.bottom < safe.top)
    safe.bottom = safe.top;
  out.push_back({hwnd, safe});
}

void SplitVertical(const RECT &in, RECT &left, RECT &right) {
  const int totalWidth = Width(in);
  if (totalWidth <= 1) {
    left = right = in;
    return;
  }

  const int usable = std::max(1, totalWidth - kInnerGap);
  const int minW = std::max(kMinTileWidth, usable / 4);
  int leftW = static_cast<int>(usable * kSplitRatio);
  if (leftW < minW)
    leftW = minW;
  if (leftW > usable - minW)
    leftW = usable - minW;

  left = in;
  left.right = left.left + leftW;
  right = in;
  right.left = left.right + kInnerGap;
}

void SplitHorizontal(const RECT &in, RECT &top, RECT &bottom) {
  const int totalHeight = Height(in);
  if (totalHeight <= 1) {
    top = bottom = in;
    return;
  }

  const int usable = std::max(1, totalHeight - kInnerGap);
  const int minH = std::max(kMinTileHeight, usable / 4);
  int topH = static_cast<int>(usable * kSplitRatio);
  if (topH < minH)
    topH = minH;
  if (topH > usable - minH)
    topH = usable - minH;

  top = in;
  top.bottom = top.top + topH;
  bottom = in;
  bottom.top = top.bottom + kInnerGap;
}

void LayoutDwindleInArea(std::vector<WindowRect> &out,
                         const std::vector<HWND> &windows, const RECT &area) {
  if (windows.empty())
    return;

  RECT remaining = area;
  const int count = static_cast<int>(windows.size());
  if (count == 1)
    return PushCell(out, windows.front(), remaining);

  bool splitVertical = Width(remaining) >= Height(remaining);
  for (int i = 0; i < count - 1; ++i) {
    RECT first{}, second{};
    splitVertical ? SplitVertical(remaining, first, second)
                  : SplitHorizontal(remaining, first, second);
    PushCell(out, windows[i], first);
    remaining = second;
    splitVertical = !splitVertical;
  }

  PushCell(out, windows[count - 1], remaining);
}

RECT ClampRectToArea(const RECT &rect, const RECT &area) {
  RECT out = rect;

  int width = std::max(kMinTileWidth, Width(out));
  int height = std::max(kMinTileHeight, Height(out));
  width = std::min(width, Width(area));
  height = std::min(height, Height(area));

  if (out.left < area.left)
    out.left = area.left;
  if (out.top < area.top)
    out.top = area.top;

  out.right = out.left + width;
  out.bottom = out.top + height;

  if (out.right > area.right) {
    out.right = area.right;
    out.left = out.right - width;
  }
  if (out.bottom > area.bottom) {
    out.bottom = area.bottom;
    out.top = out.bottom - height;
  }

  return out;
}

RECT LargestRegionAroundAnchor(const RECT &workArea, const RECT &anchor) {
  RECT candidates[4] = {
      {workArea.left, workArea.top, anchor.left - kInnerGap, workArea.bottom},
      {anchor.right + kInnerGap, workArea.top, workArea.right, workArea.bottom},
      {workArea.left, workArea.top, workArea.right, anchor.top - kInnerGap},
      {workArea.left, anchor.bottom + kInnerGap, workArea.right,
       workArea.bottom},
  };

  int best = -1, bestArea = -1;
  for (int i = 0; i < 4; ++i) {
    if (candidates[i].right < candidates[i].left)
      candidates[i].right = candidates[i].left;
    if (candidates[i].bottom < candidates[i].top)
      candidates[i].bottom = candidates[i].top;

    const int area = Area(candidates[i]);
    if (area > bestArea) {
      bestArea = area;
      best = i;
    }
  }

  return best >= 0 ? candidates[best] : workArea;
}

void BuildMonitorBuckets(const std::vector<HWND> &windows,
                         std::vector<MonitorBucket> &buckets) {
  buckets.reserve(4);
  for (HWND hwnd : windows) {
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    auto it = std::find_if(buckets.begin(), buckets.end(),
                           [monitor](const MonitorBucket &bucket) {
                             return bucket.monitor == monitor;
                           });

    if (it == buckets.end()) {
      buckets.push_back({monitor, SafeMonitorWorkArea(monitor), {hwnd}});
    } else {
      it->windows.push_back(hwnd);
    }
  }
}

BOOL CALLBACK CollectWindowsProc(HWND hwnd, LPARAM lParam) {
  auto *context = reinterpret_cast<EnumContext *>(lParam);
  if (!context || !context->windows || !context->filter)
    return TRUE;
  if (context->filter(hwnd))
    context->windows->push_back(hwnd);
  return TRUE;
}

} // namespace

std::vector<WindowRect> calculateWindowResolution(
    const std::vector<HWND> &windows) {
  return calculateWindowResolutionWithAnchor(windows, nullptr, nullptr,
                                             nullptr);
}

std::vector<WindowRect> calculateWindowResolutionWithAnchor(
    const std::vector<HWND> &windows, HWND anchorHwnd, const RECT *anchorRect,
    HWND fullscreenHwnd) {
  std::vector<WindowRect> result;
  if (windows.empty())
    return result;

  std::vector<MonitorBucket> buckets;
  BuildMonitorBuckets(windows, buckets);

  const HWND foreground = GetForegroundWindow();
  const bool hasAnchor = anchorHwnd && anchorRect && IsWindow(anchorHwnd);

  for (auto &bucket : buckets) {
    auto focused = std::find(bucket.windows.begin(), bucket.windows.end(), foreground);
    if (focused != bucket.windows.end() && focused != bucket.windows.begin())
      std::rotate(bucket.windows.begin(), focused, focused + 1);

    const RECT tiledArea = InsetRect(bucket.workArea, kOuterGap);

    if (fullscreenHwnd &&
        std::find(bucket.windows.begin(), bucket.windows.end(), fullscreenHwnd) != bucket.windows.end()) {
      PushCell(result, fullscreenHwnd, tiledArea);
      continue;
    }

    if (bucket.windows.size() == 1) {
      PushCell(result, bucket.windows.front(), tiledArea);
      continue;
    }

    if (!hasAnchor || std::find(bucket.windows.begin(), bucket.windows.end(), anchorHwnd) == bucket.windows.end()) {
      LayoutDwindleInArea(result, bucket.windows, tiledArea);
      continue;
    }

    const RECT fixedAnchor = ClampRectToArea(*anchorRect, tiledArea);
    PushCell(result, anchorHwnd, fixedAnchor);

    std::vector<HWND> others;
    others.reserve(bucket.windows.size());
    for (HWND hwnd : bucket.windows)
      if (hwnd != anchorHwnd)
        others.push_back(hwnd);
    if (others.empty())
      continue;

    const RECT leftover = LargestRegionAroundAnchor(tiledArea, fixedAnchor);
    if (Area(leftover) < kMinLeftoverArea)
      continue;

    LayoutDwindleInArea(result, others, leftover);
  }

  return result;
}

void RecalculateAndApplyLayout(WindowFilterFn filter, HWND anchorHwnd, const RECT *anchorRect, int animationDurationMs, HWND skipApplyWindow, RECT *skippedTargetRect, HWND fullscreenWindow) {
  if (!filter || g_isApplyingLayout)
    return;
  g_isApplyingLayout = true;

  std::vector<HWND> windows;
  EnumContext context{&windows, filter};
  EnumWindows(CollectWindowsProc, reinterpret_cast<LPARAM>(&context));

  const auto layout = calculateWindowResolutionWithAnchor(windows, anchorHwnd, anchorRect, fullscreenWindow);

  for (const auto &item : layout) {
    if (!IsWindow(item.hwnd))
      continue;

    if (item.hwnd == skipApplyWindow) {
      if (skippedTargetRect)
        *skippedTargetRect = item.rect;
      continue;
    }

    RECT current{};
    if (!GetWindowRect(item.hwnd, &current) || EqualRect(&current, &item.rect))
      continue;

    const int width = item.rect.right - item.rect.left;
    const int height = item.rect.bottom - item.rect.top;

    if (animationDurationMs > 0) {
      AnimateWindowTransform(item.hwnd, item.rect.left, item.rect.top, width, height, animationDurationMs);
      continue;
    }

    SetWindowPos(item.hwnd, nullptr, item.rect.left, item.rect.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
  }

  g_isApplyingLayout = false;
}
