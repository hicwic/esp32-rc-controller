#pragma once

#include <Arduino.h>
#include <Bluepad32.h>

#include "rc_model.h"

namespace rcctl {

// Runs one control iteration:
// - consumes fresh gamepad data
// - applies failsafe on signal timeout
// - applies neutral outputs when disconnected
void processControlTick(ControllerPtr gamepad, uint32_t nowMs, uint32_t signalTimeoutMs, uint32_t* lastPacketMs, bool* signalTimedOut);
// Utility used by periodic diagnostics.
int countActiveOutputs(const OutputChannelConfig* outputs);

}  // namespace rcctl
