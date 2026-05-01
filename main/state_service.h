#pragma once

#include <Arduino.h>
#include <WebServer.h>

#include "preset_store.h"
#include "rc_model.h"

namespace rcctl {

struct StateSnapshot {
    // Network/UI status.
    IPAddress apIp;
    String apSsid;
    String apPassword;
    bool gamepadConnected;
    bool btScanActive;
    bool pairingEnabled;
    bool failsafeActive;
    String fwVersion;
    String fwChannel;
    String currentModel;
    String bootModel;
    bool modelDirty;
    // Runtime/model references (owned by caller).
    const VirtualInputConfig* virtualInputs;
    const OutputChannelConfig* outputs;
    const float* virtualRuntime;
    const float* outputRuntime;
    const PresetDirectory* presetDir;
    const char* presetBuiltinA;
    const char* presetBuiltinB;
    const char* presetBuiltinC;
};

// Standard API response envelope: {"ok":..., "message":...}
void sendJson(WebServer& server, bool ok, const String& message = "");
// Static input definitions used by the UI dropdowns.
String buildInputsJson();
// Full state payload used for "slow" UI refresh (cards, presets, settings).
String buildStateJson(const StateSnapshot& s);
// Lightweight payload used for frequent activity/status updates.
String buildActivityJson(const StateSnapshot& s);

}  // namespace rcctl
