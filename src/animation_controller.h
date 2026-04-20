#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "state.h"

bool HandleAnimationTimerMessage(AppState &state, const MSG &msg);
