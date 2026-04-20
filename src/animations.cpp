#include "animations.h"
#include <unordered_map>

namespace {

struct AnimationRequest {
  HWND hwnd;
  int fromX, fromY, fromW, fromH, toX, toY, toW, toH, durationMs;
  unsigned long long token;
};

CRITICAL_SECTION g_lock;
std::unordered_map<HWND, unsigned long long> g_tokens;

void InitLock() {
  static bool initialized = false;
  if (!initialized)
    InitializeCriticalSection(&g_lock), initialized = true;
}

unsigned long long NextToken(HWND hwnd) {
  InitLock();
  EnterCriticalSection(&g_lock);
  auto token = ++g_tokens[hwnd];
  LeaveCriticalSection(&g_lock);
  return token;
}

bool IsCurrentToken(HWND hwnd, unsigned long long token) {
  InitLock();
  EnterCriticalSection(&g_lock);
  bool result = g_tokens[hwnd] == token;
  LeaveCriticalSection(&g_lock);
  return result;
}

double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

double EaseInOutCubic(double t) {
  t = Clamp01(t);
  if (t < 0.5)
    return 4.0 * t * t * t;
  double f = -2.0 * t + 2.0;
  return 1.0 - (f * f * f) / 2.0;
}

int LerpInt(int a, int b, double t) { return static_cast<int>(a + (b - a) * t); }

bool GetNowMs(double &outMs, const LARGE_INTEGER &freq) {
  LARGE_INTEGER now{};
  if (!QueryPerformanceCounter(&now) || freq.QuadPart == 0)
    return false;
  outMs = (static_cast<double>(now.QuadPart) * 1000.0) / static_cast<double>(freq.QuadPart);
  return true;
}

DWORD WINAPI AnimationThreadProc(LPVOID param) {
  auto *req = reinterpret_cast<AnimationRequest *>(param);
  if (!req)
    return 0;

  if (!IsWindow(req->hwnd) || req->durationMs <= 0) {
    if (IsWindow(req->hwnd))
      SetWindowPos(req->hwnd, nullptr, req->toX, req->toY, req->toW, req->toH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    delete req;
    return 0;
  }

  LARGE_INTEGER freq{};
  if (!QueryPerformanceFrequency(&freq)) {
    SetWindowPos(req->hwnd, nullptr, req->toX, req->toY, req->toW, req->toH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    delete req;
    return 0;
  }

  double startMs = 0.0;
  if (!GetNowMs(startMs, freq)) {
    SetWindowPos(req->hwnd, nullptr, req->toX, req->toY, req->toW, req->toH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    delete req;
    return 0;
  }

  for (;;) {
    if (!IsWindow(req->hwnd) || !IsCurrentToken(req->hwnd, req->token))
      break;

    double nowMs = 0.0;
    if (!GetNowMs(nowMs, freq))
      break;

    double t = (nowMs - startMs) / static_cast<double>(req->durationMs);
    double eased = EaseInOutCubic(t);

    SetWindowPos(req->hwnd, nullptr,
                 LerpInt(req->fromX, req->toX, eased), LerpInt(req->fromY, req->toY, eased),
                 LerpInt(req->fromW, req->toW, eased), LerpInt(req->fromH, req->toH, eased),
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);

    if (t >= 1.0)
      break;
    Sleep(8);
  }

  if (IsWindow(req->hwnd) && IsCurrentToken(req->hwnd, req->token))
    SetWindowPos(req->hwnd, nullptr, req->toX, req->toY, req->toW, req->toH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSENDCHANGING);

  delete req;
  return 0;
}

void StartAnimation(const AnimationRequest &request) {
  HANDLE thread = CreateThread(nullptr, 0, AnimationThreadProc, new AnimationRequest(request), 0, nullptr);
  if (thread)
    CloseHandle(thread);
}

} // namespace

void AnimateWindowTransform(HWND hwnd, int x, int y, int width, int height, int durationMs) {
  if (!hwnd || !IsWindow(hwnd))
    return;
  RECT rect{};
  GetWindowRect(hwnd, &rect);
  StartAnimation({hwnd, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top, x, y, width, height, durationMs, NextToken(hwnd)});
}

void AnimateWindowMove(HWND hwnd, int x, int y, int durationMs) {
  if (!hwnd || !IsWindow(hwnd))
    return;
  RECT rect{};
  GetWindowRect(hwnd, &rect);
  AnimateWindowTransform(hwnd, x, y, rect.right - rect.left, rect.bottom - rect.top, durationMs);
}

void AnimateWindowResize(HWND hwnd, int width, int height, int durationMs) {
  if (!hwnd || !IsWindow(hwnd))
    return;
  RECT rect{};
  GetWindowRect(hwnd, &rect);
  AnimateWindowTransform(hwnd, rect.left, rect.top, width, height, durationMs);
}
