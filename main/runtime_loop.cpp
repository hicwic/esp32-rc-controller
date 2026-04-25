#include "runtime_loop.h"

namespace rcctl {

void processControlTick(ControllerPtr gamepad, uint32_t nowMs, uint32_t signalTimeoutMs, uint32_t* lastPacketMs, bool* signalTimedOut) {
    if (gamepad && gamepad->isConnected()) {
        if (gamepad->hasData()) {
            if (lastPacketMs) {
                *lastPacketMs = nowMs;
            }
            if (signalTimedOut) {
                *signalTimedOut = false;
            }
            processGamepadToOutputs(gamepad);
            return;
        }
        // If no fresh packet arrives for too long, force a safe output state.
        const uint32_t last = lastPacketMs ? *lastPacketMs : nowMs;
        if (nowMs - last > signalTimeoutMs) {
            applyFailsafeAllOutputs();
            if (signalTimedOut) {
                *signalTimedOut = true;
            }
        }
        return;
    }

    evaluateVirtualRuntime(nullptr);
    applyFailsafeAllOutputs();
}

int countActiveOutputs(const OutputChannelConfig* outputs) {
    if (!outputs) {
        return 0;
    }
    int active = 0;
    for (int i = 0; i < kMaxOutputChannels; ++i) {
        if (outputs[i].used) {
            active++;
        }
    }
    return active;
}

}  // namespace rcctl
