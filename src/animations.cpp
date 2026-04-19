#include "animations.h"

#include <vector>

namespace {

struct AnimationRequest {
  HWND hwnd;
  int fromX;
  int fromY;
  int fromW;
  int fromH;
  int toX;
  int toY;
  int toW;
  int toH;
  int durationMs;
  unsigned long long token;
};

struct AnimationToken {
  HWND hwnd;
  unsigned long long token;
};

CRITICAL_SECTION g_lock;
bool g_lockInitialized = false;
std::vector<AnimationToken> g_tokens;

inline void ApplyWindowPos(HWND hwnd, int x, int y, int w, int h) {
  SetWindowPos(hwnd, nullptr, x, y, w, h,
               SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
}

void EnsureLock() {
  if (g_lockInitialized) return;
  InitializeCriticalSection(&g_lock);
  g_lockInitialized = true;
}

unsigned long long NextToken(HWND hwnd) {
  EnsureLock();
  EnterCriticalSection(&g_lock);

  for (auto& item : g_tokens) {
    if (item.hwnd == hwnd) {
      const auto token = ++item.token;
      LeaveCriticalSection(&g_lock);
      return token;
    }
  }

  g_tokens.push_back({hwnd, 1});
  LeaveCriticalSection(&g_lock);
  return 1;
}

bool IsCurrentToken(HWND hwnd, unsigned long long token) {
  EnsureLock();
  EnterCriticalSection(&g_lock);

  for (const auto& item : g_tokens) {
    if (item.hwnd == hwnd) {
      const bool isCurrent = item.token == token;
      LeaveCriticalSection(&g_lock);
      return isCurrent;
    }
  }

  LeaveCriticalSection(&g_lock);
  return false;
}

inline double Clamp01(double v) {
  if (v < 0.0) return 0.0;
  if (v > 1.0) return 1.0;
  return v;
}

inline double EaseInOutCubic(double t) {
  t = Clamp01(t);
  if (t < 0.5) return 4.0 * t * t * t;
  const double f = -2.0 * t + 2.0;
  return 1.0 - (f * f * f) / 2.0;
}

inline int LerpInt(int a, int b, double t) {
  return static_cast<int>(a + (b - a) * t);
}

bool QueryNowMs(double* outMs, const LARGE_INTEGER& freq) {
  LARGE_INTEGER now{};
  if (!QueryPerformanceCounter(&now)) return false;
  *outMs = (static_cast<double>(now.QuadPart) * 1000.0) /
           static_cast<double>(freq.QuadPart);
  return true;
}

DWORD WINAPI AnimationThreadProc(LPVOID param) {
  auto* req = reinterpret_cast<AnimationRequest*>(param);
  if (!req) return 0;

  if (!IsWindow(req->hwnd) || req->durationMs <= 0) {
    if (IsWindow(req->hwnd))
      ApplyWindowPos(req->hwnd, req->toX, req->toY, req->toW, req->toH);
    delete req;
    return 0;
  }

  LARGE_INTEGER freq{};
  if (!QueryPerformanceFrequency(&freq) || freq.QuadPart == 0) {
    ApplyWindowPos(req->hwnd, req->toX, req->toY, req->toW, req->toH);
    delete req;
    return 0;
  }

  double startMs = 0.0;
  if (!QueryNowMs(&startMs, freq)) {
    ApplyWindowPos(req->hwnd, req->toX, req->toY, req->toW, req->toH);
    delete req;
    return 0;
  }

  for (;;) {
    if (!IsWindow(req->hwnd) || !IsCurrentToken(req->hwnd, req->token)) break;

    double nowMs = 0.0;
    if (!QueryNowMs(&nowMs, freq)) break;

    const double t =
        Clamp01((nowMs - startMs) / static_cast<double>(req->durationMs));
    const double eased = EaseInOutCubic(t);

    ApplyWindowPos(req->hwnd, LerpInt(req->fromX, req->toX, eased),
                   LerpInt(req->fromY, req->toY, eased),
                   LerpInt(req->fromW, req->toW, eased),
                   LerpInt(req->fromH, req->toH, eased));

    if (t >= 1.0) break;
    Sleep(8);
  }

  if (IsWindow(req->hwnd) && IsCurrentToken(req->hwnd, req->token))
    ApplyWindowPos(req->hwnd, req->toX, req->toY, req->toW, req->toH);

  delete req;
  return 0;
}

void StartAnimation(const AnimationRequest& request) {
  auto* heapReq = new AnimationRequest(request);
  HANDLE thread =
      CreateThread(nullptr, 0, AnimationThreadProc, heapReq, 0, nullptr);

  if (thread) {
    CloseHandle(thread);
    return;
  }

  delete heapReq;
}

}  // namespace

void AnimateWindowTransform(HWND hwnd, int x, int y, int width, int height,
                            int durationMs) {
  if (!hwnd || !IsWindow(hwnd)) return;

  RECT rect{};
  if (!GetWindowRect(hwnd, &rect)) return;

  StartAnimation({hwnd,
                  rect.left,
                  rect.top,
                  rect.right - rect.left,
                  rect.bottom - rect.top,
                  x,
                  y,
                  width,
                  height,
                  durationMs,
                  NextToken(hwnd)});
}

void AnimateWindowMove(HWND hwnd, int x, int y, int durationMs) {
  if (!hwnd || !IsWindow(hwnd)) return;

  RECT rect{};
  if (!GetWindowRect(hwnd, &rect)) return;

  AnimateWindowTransform(hwnd, x, y, rect.right - rect.left,
                         rect.bottom - rect.top, durationMs);
}

void AnimateWindowResize(HWND hwnd, int width, int height, int durationMs) {
  if (!hwnd || !IsWindow(hwnd)) return;

  RECT rect{};
  if (!GetWindowRect(hwnd, &rect)) return;

  AnimateWindowTransform(hwnd, rect.left, rect.top, width, height, durationMs);
}
