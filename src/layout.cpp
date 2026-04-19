#include "layout.h"

#include <algorithm>
#include <iostream>
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
  std::vector<HWND>* windows = nullptr;
  WindowFilterFn filter = nullptr;
};

bool g_isApplyingLayout = false;

int Width(const RECT& rect) { return rect.right - rect.left; }
int Height(const RECT& rect) { return rect.bottom - rect.top; }
int Area(const RECT& rect) {
  const int w = rect.right - rect.left, h = rect.bottom - rect.top;
  return (w > 0 && h > 0) ? w * h : 0;
}

RECT InsetRect(const RECT& rect, int inset) {
  RECT result = rect;
  result.left += inset;
  result.top += inset;
  result.right -= inset;
  result.bottom -= inset;

  if (result.right < result.left) result.right = result.left;
  if (result.bottom < result.top) result.bottom = result.top;

  return result;
}

RECT SafeMonitorWorkArea(HMONITOR monitor) {
  MONITORINFO monitorInfo{};
  monitorInfo.cbSize = sizeof(monitorInfo);
  if (monitor && GetMonitorInfo(monitor, &monitorInfo))
    return monitorInfo.rcWork;
  return {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

void PushCell(std::vector<WindowRect>& out, HWND hwnd, const RECT& rect) {
  RECT safeRect = rect;
  if (safeRect.right < safeRect.left) safeRect.right = safeRect.left;
  if (safeRect.bottom < safeRect.top) safeRect.bottom = safeRect.top;
  out.push_back({hwnd, safeRect});
}

void SplitVertical(const RECT& in, RECT& left, RECT& right) {
  const int totalWidth = Width(in);
  if (totalWidth <= 1) {
    left = in;
    right = in;
    return;
  }

  const int usableWidth = std::max(1, totalWidth - kInnerGap);
  int leftWidth = static_cast<int>(usableWidth * kSplitRatio);
  const int minWidth = std::max(kMinTileWidth, usableWidth / 4);

  if (leftWidth < minWidth) leftWidth = minWidth;
  if (leftWidth > usableWidth - minWidth) leftWidth = usableWidth - minWidth;

  left = in;
  left.right = left.left + leftWidth;

  right = in;
  right.left = left.right + kInnerGap;
}

void SplitHorizontal(const RECT& in, RECT& top, RECT& bottom) {
  const int totalHeight = Height(in);
  if (totalHeight <= 1) {
    top = in;
    bottom = in;
    return;
  }

  const int usableHeight = std::max(1, totalHeight - kInnerGap);
  int topHeight = static_cast<int>(usableHeight * kSplitRatio);
  const int minHeight = std::max(kMinTileHeight, usableHeight / 4);

  if (topHeight < minHeight) topHeight = minHeight;
  if (topHeight > usableHeight - minHeight)
    topHeight = usableHeight - minHeight;

  top = in;
  top.bottom = top.top + topHeight;

  bottom = in;
  bottom.top = top.bottom + kInnerGap;
}

void LayoutDwindleInArea(std::vector<WindowRect>& out,
                         const std::vector<HWND>& windows, const RECT& area) {
  if (windows.empty()) return;

  RECT remaining = area;
  const int windowCount = static_cast<int>(windows.size());

  if (windowCount == 1) {
    PushCell(out, windows[0], remaining);
    return;
  }

  bool splitVertical = Width(remaining) >= Height(remaining);

  for (int i = 0; i < windowCount - 1; ++i) {
    RECT first{}, second{};

    if (splitVertical) {
      SplitVertical(remaining, first, second);
    } else {
      SplitHorizontal(remaining, first, second);
    }

    PushCell(out, windows[i], first);
    remaining = second;
    splitVertical = !splitVertical;
  }

  PushCell(out, windows[windowCount - 1], remaining);
}

RECT ClampRectToArea(const RECT& rect, const RECT& area) {
  RECT out = rect;

  int width = Width(out);
  int height = Height(out);
  if (width < kMinTileWidth) width = kMinTileWidth;
  if (height < kMinTileHeight) height = kMinTileHeight;

  width = std::min(width, Width(area));
  height = std::min(height, Height(area));

  if (out.left < area.left) out.left = area.left;
  if (out.top < area.top) out.top = area.top;

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

RECT LargestRegionAroundAnchor(const RECT& workArea, const RECT& anchor) {
  RECT candidates[4] = {
      {workArea.left, workArea.top, anchor.left - kInnerGap, workArea.bottom},
      {anchor.right + kInnerGap, workArea.top, workArea.right, workArea.bottom},
      {workArea.left, workArea.top, workArea.right, anchor.top - kInnerGap},
      {workArea.left, anchor.bottom + kInnerGap, workArea.right,
       workArea.bottom},
  };

  int bestIndex = -1;
  int bestArea = -1;

  for (int i = 0; i < 4; ++i) {
    if (candidates[i].right < candidates[i].left) {
      candidates[i].right = candidates[i].left;
    }
    if (candidates[i].bottom < candidates[i].top) {
      candidates[i].bottom = candidates[i].top;
    }

    const int currentArea = Area(candidates[i]);
    if (currentArea > bestArea) {
      bestArea = currentArea;
      bestIndex = i;
    }
  }

  return bestIndex >= 0 ? candidates[bestIndex] : workArea;
}

void BuildMonitorBuckets(const std::vector<HWND>& windows,
                         std::vector<MonitorBucket>& buckets) {
  buckets.reserve(4);
  for (HWND hwnd : windows) {
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

    auto it = std::find_if(buckets.begin(), buckets.end(),
                           [monitor](const MonitorBucket& bucket) {
                             return bucket.monitor == monitor;
                           });

    if (it == buckets.end()) {
      MonitorBucket bucket;
      bucket.monitor = monitor;
      bucket.workArea = SafeMonitorWorkArea(monitor);
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

  const HWND foreground = GetForegroundWindow();
  const bool hasAnchor = anchorHwnd && anchorRect && IsWindow(anchorHwnd);

  for (auto& bucket : buckets) {
    auto focusedWindow =
        std::find(bucket.windows.begin(), bucket.windows.end(), foreground);
    if (focusedWindow != bucket.windows.end() &&
        focusedWindow != bucket.windows.begin()) {
      std::rotate(bucket.windows.begin(), focusedWindow, focusedWindow + 1);
    }

    const RECT tiledArea = InsetRect(bucket.workArea, kOuterGap);

    if (!hasAnchor || std::find(bucket.windows.begin(), bucket.windows.end(),
                                anchorHwnd) == bucket.windows.end()) {
      LayoutDwindleInArea(result, bucket.windows, tiledArea);
      continue;
    }

    const RECT fixedAnchor = ClampRectToArea(*anchorRect, tiledArea);
    PushCell(result, anchorHwnd, fixedAnchor);

    std::vector<HWND> otherWindows;
    otherWindows.reserve(bucket.windows.size());
    for (HWND hwnd : bucket.windows) {
      if (hwnd != anchorHwnd) otherWindows.push_back(hwnd);
    }

    if (otherWindows.empty()) continue;

    const RECT leftover = LargestRegionAroundAnchor(tiledArea, fixedAnchor);
    if (Area(leftover) < kMinLeftoverArea) continue;

    LayoutDwindleInArea(result, otherWindows, leftover);
  }

  return result;
}

void RecalculateAndApplyLayout(WindowFilterFn filter, HWND anchorHwnd,
                               const RECT* anchorRect, int animationDurationMs,
                               HWND skipApplyWindow, RECT* skippedTargetRect) {
  if (!filter || g_isApplyingLayout) return;

  g_isApplyingLayout = true;

  std::vector<HWND> windows;
  EnumContext context{&windows, filter};
  EnumWindows(CollectWindowsProc, reinterpret_cast<LPARAM>(&context));

  const auto layout =
      calculateWindowResolutionWithAnchor(windows, anchorHwnd, anchorRect);

  int movedCount = 0;
  for (const auto& item : layout) {
    if (!IsWindow(item.hwnd)) continue;

    if (item.hwnd == skipApplyWindow) {
      if (skippedTargetRect) *skippedTargetRect = item.rect;
      continue;
    }

    RECT currentRect{};
    if (!GetWindowRect(item.hwnd, &currentRect) ||
        EqualRect(&currentRect, &item.rect)) {
      continue;
    }

    const int width = item.rect.right - item.rect.left;
    const int height = item.rect.bottom - item.rect.top;

    if (animationDurationMs > 0) {
      AnimateWindowTransform(item.hwnd, item.rect.left, item.rect.top, width,
                             height, animationDurationMs);
      ++movedCount;
      continue;
    }

    if (SetWindowPos(item.hwnd, nullptr, item.rect.left, item.rect.top, width,
                     height,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING)) {
      ++movedCount;
    }
  }

  std::wcout << L"Layout computed for " << layout.size()
             << L" window(s), moved " << movedCount << L"." << std::endl;

  g_isApplyingLayout = false;
}
