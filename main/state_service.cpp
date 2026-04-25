#include "state_service.h"

#include <ESP32Servo.h>
#include <WiFi.h>
#include <math.h>

#include "control_inputs.h"
#include "json_utils.h"

namespace rcctl {

namespace {

constexpr float kActiveThreshold = 0.15f;

void appendVirtualInput(JsonWriter& j, int index, const VirtualInputConfig& in, float runtime) {
    j.beginObject();
    j.key("index");
    j.value(index);
    j.key("name");
    j.value(String(in.name));
    j.key("input");
    j.value(static_cast<int>(in.primary));
    j.key("input_secondary");
    j.value(static_cast<int>(in.secondary));
    j.key("input_type");
    j.value(static_cast<int>(in.inputType));
    j.key("range_mode");
    j.value(static_cast<int>(in.toggleRange));
    j.key("rumble");
    j.value(in.rumbleEnabled);
    j.key("input_modifier");
    j.value(static_cast<int>(in.modifier));
    j.key("modifier_function");
    j.value(static_cast<int>(in.modifierFunction));
    j.key("deadzone");
    j.value(in.deadzonePercent);
    j.key("expo");
    j.value(in.expoPercent);
    j.key("signed_activity");
    j.value(runtime, 3);
    j.key("activity");
    j.value(fabsf(runtime), 3);
    j.key("active");
    j.value(fabsf(runtime) > kActiveThreshold);
    j.endObject();
}

void appendOutput(JsonWriter& j, int index, const OutputChannelConfig& out, float runtime) {
    j.beginObject();
    j.key("index");
    j.value(index);
    j.key("name");
    j.value(String(out.name));
    j.key("type");
    j.value(static_cast<int>(out.type));
    j.key("type_label");
    j.value(outputTypeLabel(out.type));
    j.key("pin");
    j.value(out.pin);
    j.key("source_a");
    j.value(out.sourceA);
    j.key("source_b");
    j.value(out.sourceB);
    j.key("source_c");
    j.value(out.sourceC);
    j.key("mix_mode");
    j.value(static_cast<int>(out.mixMode));
    j.key("mix_mode_label");
    j.value(mixModeLabel(out.mixMode));
    j.key("weight_a");
    j.value(out.weightA);
    j.key("weight_b");
    j.value(out.weightB);
    j.key("weight_c");
    j.value(out.weightC);
    j.key("offset_a");
    j.value(out.offsetA);
    j.key("offset_b");
    j.value(out.offsetB);
    j.key("offset_c");
    j.value(out.offsetC);
    j.key("threshold");
    j.value(out.thresholdPercent);
    j.key("inverted");
    j.value(out.inverted);
    j.key("signed_activity");
    j.value(runtime, 3);
    j.key("activity");
    j.value(fabsf(runtime), 3);
    j.key("active");
    j.value(fabsf(runtime) > kActiveThreshold);
    j.endObject();
}

}  // namespace

void sendJson(WebServer& server, bool ok, const String& message) {
    JsonWriter j;
    j.beginObject();
    j.key("ok");
    j.value(ok);
    if (message.length()) {
        j.key("message");
        j.value(message);
    }
    j.endObject();
    server.send(ok ? 200 : 400, "application/json", j.str());
}

String buildInputsJson() {
    JsonWriter j;
    j.beginArray();
    for (size_t i = 0; i < kInputCount; ++i) {
        if (kInputs[i].id == InputId::ButtonL2 || kInputs[i].id == InputId::ButtonR2) {
            continue;
        }
        j.beginObject();
        j.key("id");
        j.value(static_cast<int>(kInputs[i].id));
        j.key("label");
        j.value(String(kInputs[i].label));
        j.endObject();
    }
    j.endArray();
    return j.str();
}

String buildStateJson(const StateSnapshot& s) {
    // This payload is intentionally verbose: it drives all static UI rendering.
    JsonWriter j;
    j.beginObject();
    j.key("ap_ip");
    j.value(s.apIp.toString());
    j.key("ap_ssid");
    j.value(s.apSsid);
    j.key("ap_password");
    j.value(s.apPassword);
    j.key("gamepad");
    j.value(s.gamepadConnected);
    j.key("bt_scan");
    j.value(s.btScanActive);
    j.key("pairing");
    j.value(s.pairingEnabled);
    j.key("current_model");
    j.value(s.currentModel);
    j.key("boot_model");
    j.value(s.bootModel);
    j.key("model_dirty");
    j.value(s.modelDirty);

    j.key("virtual_inputs");
    j.beginArray();
    for (int i = 0; i < kMaxVirtualInputs; ++i) {
        if (!s.virtualInputs[i].used) {
            continue;
        }
        appendVirtualInput(j, i, s.virtualInputs[i], s.virtualRuntime[i]);
    }
    j.endArray();

    j.key("outputs");
    j.beginArray();
    for (int i = 0; i < kMaxOutputChannels; ++i) {
        if (!s.outputs[i].used) {
            continue;
        }
        appendOutput(j, i, s.outputs[i], s.outputRuntime[i]);
    }
    j.endArray();

    j.key("pwm_pins");
    j.beginArray();
    for (int pin = 0; pin <= 48; ++pin) {
        if (ESP32PWM::hasPwm(pin)) {
            j.value(pin);
        }
    }
    j.endArray();

    j.key("presets");
    j.beginArray();
    j.value(s.presetBuiltinA);
    j.value(s.presetBuiltinB);
    j.value(s.presetBuiltinC);
    if (s.presetDir) {
        for (int i = 0; i < static_cast<int>(s.presetDir->count); ++i) {
            j.value(String(s.presetDir->names[i]));
        }
    }
    j.endArray();
    j.endObject();
    return j.str();
}

String buildActivityJson(const StateSnapshot& s) {
    // Keep this payload small: it is polled at high frequency for live bars/chips.
    JsonWriter j;
    j.beginObject();
    j.key("ap_ssid");
    j.value(s.apSsid);
    j.key("gamepad");
    j.value(s.gamepadConnected);
    j.key("bt_scan");
    j.value(s.btScanActive);
    j.key("pairing");
    j.value(s.pairingEnabled);

    j.key("virtual_inputs");
    j.beginArray();
    for (int i = 0; i < kMaxVirtualInputs; ++i) {
        if (!s.virtualInputs[i].used) {
            continue;
        }
        j.beginObject();
        j.key("index");
        j.value(i);
        j.key("signed_activity");
        j.value(s.virtualRuntime[i], 3);
        j.key("active");
        j.value(fabsf(s.virtualRuntime[i]) > kActiveThreshold);
        j.endObject();
    }
    j.endArray();

    j.key("outputs");
    j.beginArray();
    for (int i = 0; i < kMaxOutputChannels; ++i) {
        if (!s.outputs[i].used) {
            continue;
        }
        j.beginObject();
        j.key("index");
        j.value(i);
        j.key("signed_activity");
        j.value(s.outputRuntime[i], 3);
        j.key("active");
        j.value(fabsf(s.outputRuntime[i]) > kActiveThreshold);
        j.endObject();
    }
    j.endArray();
    j.endObject();
    return j.str();
}

}  // namespace rcctl
