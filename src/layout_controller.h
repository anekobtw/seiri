#pragma once

#include "state.h"

void QueueRelayout(AppState& state);
bool HandleRelayoutMessage(AppState& state);
void ApplyLayout(AppState& state);
