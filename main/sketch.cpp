// SPDX-License-Identifier: Apache-2.0

#include "sdkconfig.h"

#include <Arduino.h>
#include <AsyncUDP.h>  // Force Arduino component dependency discovery for DNSServer.
#include <Bluepad32.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_coexist.h>
#include <esp_wifi.h>

#include "control_inputs.h"
#include "preset_service.h"
#include "preset_store.h"
#include "rc_model.h"
#include "runtime_loop.h"
#include "state_service.h"
#include "web_routes.h"
#include "web_ui.h"

#include <math.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace {

using rcctl::PersistedConfig;
using rcctl::PresetDirectory;
using rcctl::VirtualInputConfig;
using rcctl::OutputChannelConfig;
using rcctl::kMaxOutputChannels;
using rcctl::kMaxVirtualInputs;
using rcctl::kPresetExcavator;
using rcctl::kPresetRcCar;
using rcctl::kPresetSkidSteer;

constexpr uint32_t kSignalTimeoutMs = 450;
constexpr uint32_t kUiRefreshMs = 2500;
constexpr uint32_t kIoTraceMs = 350;

constexpr const char* kDefaultApSsid = "RC-Controller";
constexpr const char* kDefaultApPassword = "rccontrol";
constexpr const char* kApSsidNvsKey = "ap_ssid";
constexpr const char* kApPasswordNvsKey = "ap_pass";
constexpr uint8_t kApChannel = 1;
constexpr uint8_t kApMaxConnections = 4;
constexpr bool kDiagDisableBluepad32 = false;
constexpr uint32_t kPairingWindowMs = 120000;
constexpr uint32_t kBtAutoScanOnMs = 1500;
constexpr uint32_t kBtAutoScanOffMs = 12000;
constexpr uint32_t kBtPairingScanOnMs = 3500;
constexpr uint32_t kBtPairingScanOffMs = 900;

#ifndef RCCTL_ENABLE_IO_TRACE
#ifdef DEBUG
#define RCCTL_ENABLE_IO_TRACE 1
#else
#define RCCTL_ENABLE_IO_TRACE 0
#endif
#endif

#ifdef DEBUG
#define RCCTL_DEBUG_LOG 1
#else
#define RCCTL_DEBUG_LOG 0
#endif

ControllerPtr g_gamepad = nullptr;
uint32_t g_lastPacketMs = 0;
uint32_t g_lastUiLogMs = 0;
uint32_t g_lastIoTraceMs = 0;
bool g_signalTimedOut = false;
bool g_pairingScanEnabled = false;
uint32_t g_pairingScanUntilMs = 0;
bool g_btScanActive = false;
uint32_t g_btScanToggleAtMs = 0;

String g_currentModelName = kPresetRcCar;
String g_bootModelName = kPresetRcCar;
String g_apSsid = kDefaultApSsid;
String g_apPassword = kDefaultApPassword;
bool g_modelDirty = false;

WebServer g_server(80);
DNSServer g_dnsServer;

VirtualInputConfig* g_virtualInputs = rcctl::virtualInputs();
OutputChannelConfig* g_outputs = rcctl::outputs();
float* g_virtualRuntime = rcctl::virtualRuntimeValues();
float* g_outputRuntime = rcctl::outputRuntimeValues();

const char* inputTypeLabel(rcctl::InputType t) {
    switch (t) {
        case rcctl::InputType::Toggle2Pos:
            return "T2";
        case rcctl::InputType::Toggle3Pos:
            return "T3";
        default:
            return "DIR";
    }
}

const char* toggleRangeLabel(rcctl::ToggleRangeMode r) {
    switch (r) {
        case rcctl::ToggleRangeMode::Bipolar:
            return "BI";
        default:
            return "UNI";
    }
}

const char* modifierFunctionLabel(rcctl::ModifierFunction f) {
    switch (f) {
        case rcctl::ModifierFunction::Reverse:
            return "REV";
        case rcctl::ModifierFunction::Center:
            return "CTR";
        default:
            return "-";
    }
}

const char* inputIdLabel(InputId id) {
    const InputDefinition* d = getInputDefinition(id);
    return d ? d->label : "None";
}

int pwmUsFromSignal(float signal) {
    const float clamped = constrain(signal, -1.0f, 1.0f);
    return static_cast<int>(lroundf(1500.0f + clamped * 500.0f));
}

void emitIoTrace(ControllerPtr ctl) {
#if RCCTL_ENABLE_IO_TRACE
    if (!ctl || !ctl->isConnected()) {
        Console.println("TRACE: gamepad=OFF");
        return;
    }

    String inLine = "TRACE IN : ";
    bool firstIn = true;
    for (int i = 0; i < kMaxVirtualInputs; ++i) {
        const auto& in = g_virtualInputs[i];
        if (!in.used) {
            continue;
        }
        if (!firstIn) {
            inLine += " | ";
        }
        firstIn = false;
        const float p = normalizedForInput(in.primary, ctl);
        const float s = (in.secondary != InputId::None) ? normalizedForInput(in.secondary, ctl) : 0.0f;
        const bool mod = (in.modifier != InputId::None) ? isInputActive(in.modifier, ctl) : false;
        inLine += "IN";
        inLine += String(i);
        inLine += "(";
        inLine += inputTypeLabel(in.inputType);
        inLine += "/";
        inLine += modifierFunctionLabel(in.modifierFunction);
        inLine += "/";
        inLine += toggleRangeLabel(in.toggleRange);
        inLine += ") ";
        inLine += String(in.name);
        inLine += " p=";
        inLine += String(p, 2);
        if (in.secondary != InputId::None) {
            inLine += " s=";
            inLine += String(s, 2);
        }
        if (in.modifier != InputId::None) {
            inLine += " m=";
            inLine += mod ? "1" : "0";
        }
        inLine += " v=";
        inLine += String(g_virtualRuntime[i], 2);
    }

    String outLine = "TRACE OUT: ";
    bool firstOut = true;
    for (int i = 0; i < kMaxOutputChannels; ++i) {
        const auto& out = g_outputs[i];
        if (!out.used) {
            continue;
        }
        if (!firstOut) {
            outLine += " | ";
        }
        firstOut = false;
        const float sig = g_outputRuntime[i];
        outLine += "CH";
        outLine += String(i);
        outLine += " ";
        outLine += String(out.name);
        outLine += " pin=";
        outLine += String(out.pin);
        outLine += " mix=(";
        outLine += String(out.sourceA);
        outLine += "*";
        outLine += String(out.weightA);
        outLine += "+";
        outLine += String(out.offsetA);
        outLine += " ";
        outLine += ",";
        outLine += String(out.sourceB);
        outLine += "*";
        outLine += String(out.weightB);
        outLine += "+";
        outLine += String(out.offsetB);
        outLine += " ";
        outLine += ",";
        outLine += String(out.sourceC);
        outLine += "*";
        outLine += String(out.weightC);
        outLine += "+";
        outLine += String(out.offsetC);
        outLine += ")";
        outLine += " mode=";
        outLine += rcctl::mixModeLabel(out.mixMode);
        outLine += " sig=";
        outLine += String(sig, 2);
        if (out.type == rcctl::ChannelType::Pwm) {
            outLine += " pwm=";
            outLine += String(pwmUsFromSignal(sig));
            outLine += "us";
        } else {
            const float analog01 = constrain(fabsf(sig), 0.0f, 1.0f);
            const float thr = constrain(out.thresholdPercent, 0, 100) / 100.0f;
            outLine += " sw=";
            outLine += (analog01 >= thr) ? "HIGH" : "LOW";
        }
    }

    Console.println(inLine);
    Console.println(outLine);
#else
    (void)ctl;
#endif
}

void setBtScanActive(bool enabled, const char* reason) {
    if (kDiagDisableBluepad32) {
        return;
    }
    if (g_btScanActive == enabled) {
        return;
    }
    BP32.enableNewBluetoothConnections(enabled);
    g_btScanActive = enabled;
#if RCCTL_DEBUG_LOG
    Console.printf("BT scan: %s (%s)\n", enabled ? "ON" : "OFF", reason);
#else
    (void)reason;
#endif
}

void updateBtScanScheduler() {
    if (kDiagDisableBluepad32) {
        return;
    }

    const uint32_t now = millis();
    const bool connected = g_gamepad && g_gamepad->isConnected();

    if (g_pairingScanEnabled && now > g_pairingScanUntilMs) {
        g_pairingScanEnabled = false;
#if RCCTL_DEBUG_LOG
        Console.println("BT pairing mode: timeout -> OFF");
#endif
        g_btScanToggleAtMs = 0;
    }

    if (connected) {
        setBtScanActive(false, "gamepad connected");
        g_btScanToggleAtMs = now + 2000;
        return;
    }

    if (g_btScanToggleAtMs != 0 && now < g_btScanToggleAtMs) {
        return;
    }

    const bool pairingMode = g_pairingScanEnabled;
    const uint32_t onMs = pairingMode ? kBtPairingScanOnMs : kBtAutoScanOnMs;
    const uint32_t offMs = pairingMode ? kBtPairingScanOffMs : kBtAutoScanOffMs;
    const char* mode = pairingMode ? "pairing pulse" : "auto-reconnect pulse";

    if (g_btScanActive) {
        setBtScanActive(false, mode);
        g_btScanToggleAtMs = now + offMs;
    } else {
        setBtScanActive(true, mode);
        g_btScanToggleAtMs = now + onMs;
    }
}

int parseIntArg(const String& name, int fallback) {
    if (!g_server.hasArg(name)) {
        return fallback;
    }
    String v = g_server.arg(name);
    v.trim();
    if (v.isEmpty()) {
        return fallback;
    }
    return v.toInt();
}

bool parseBoolArg(const String& name, bool fallback = false) {
    if (!g_server.hasArg(name)) {
        return fallback;
    }
    const String v = g_server.arg(name);
    return v == "1" || v == "true" || v == "on";
}

String sanitizeApSsid(const String& raw) {
    String ssid = raw;
    ssid.trim();
    if (ssid.isEmpty()) {
        return String(kDefaultApSsid);
    }
    if (ssid.length() > 31) {
        ssid = ssid.substring(0, 31);
    }
    return ssid;
}

String sanitizeApPassword(const String& raw) {
    String pass = raw;
    pass.trim();
    if (pass.length() > 63) {
        pass = pass.substring(0, 63);
    }
    return pass;
}

bool isValidApPassword(const String& pass) {
    if (pass.isEmpty()) {
        return true;
    }
    return pass.length() >= 8 && pass.length() <= 63;
}

String loadApSsid() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return String(kDefaultApSsid);
    }
    size_t required = 0;
    err = nvs_get_str(handle, kApSsidNvsKey, nullptr, &required);
    if (err != ESP_OK || required == 0 || required > 64) {
        nvs_close(handle);
        return String(kDefaultApSsid);
    }
    char buf[64] = {0};
    err = nvs_get_str(handle, kApSsidNvsKey, buf, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        return String(kDefaultApSsid);
    }
    return sanitizeApSsid(String(buf));
}

String loadApPassword() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return String(kDefaultApPassword);
    }
    size_t required = 0;
    err = nvs_get_str(handle, kApPasswordNvsKey, nullptr, &required);
    if (err != ESP_OK || required == 0 || required > 80) {
        nvs_close(handle);
        return String(kDefaultApPassword);
    }
    char buf[80] = {0};
    err = nvs_get_str(handle, kApPasswordNvsKey, buf, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        return String(kDefaultApPassword);
    }
    const String pass = sanitizeApPassword(String(buf));
    return isValidApPassword(pass) ? pass : String(kDefaultApPassword);
}

bool saveApSsid(const String& raw) {
    const String ssid = sanitizeApSsid(raw);
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }
    err = nvs_set_str(handle, kApSsidNvsKey, ssid.c_str());
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

bool saveApPassword(const String& raw) {
    const String pass = sanitizeApPassword(raw);
    if (!isValidApPassword(pass)) {
        return false;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }
    err = nvs_set_str(handle, kApPasswordNvsKey, pass.c_str());
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

void sendJson(bool ok, const String& message = "") {
    // Delegate all envelope serialization to a single JSON utility path.
    rcctl::sendJson(g_server, ok, message);
}

String buildInputsJson() {
    return rcctl::buildInputsJson();
}

void handleRoot() {
    streamWebUiPage(g_server);
}

void handleApiInputs() {
    g_server.send(200, "application/json", buildInputsJson());
}

void redirectCaptivePortal() {
    g_server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    g_server.sendHeader("Pragma", "no-cache");
    g_server.sendHeader("Expires", "-1");
    g_server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    g_server.send(302, "text/plain", "");
}

void parseVirtualFromRequest(VirtualInputConfig* out, int existingIndex) {
    const int fallbackPrimary = (out && out->primary != InputId::None) ? static_cast<int>(out->primary)
                                                                        : static_cast<int>(InputId::AxisX);
    const int fallbackSecondary = out ? static_cast<int>(out->secondary) : static_cast<int>(InputId::None);
    const int fallbackType = out ? static_cast<int>(out->inputType) : 0;
    const int fallbackRange = out ? static_cast<int>(out->toggleRange) : 1;
    const bool fallbackRumble = out ? out->rumbleEnabled : false;
    out->primary = static_cast<InputId>(parseIntArg("input", fallbackPrimary));
    out->secondary = static_cast<InputId>(parseIntArg("input_secondary", fallbackSecondary));
    const int type = parseIntArg("input_type", fallbackType);
    switch (type) {
        case 1:
            out->inputType = rcctl::InputType::Toggle2Pos;
            break;
        case 2:
            out->inputType = rcctl::InputType::Toggle3Pos;
            break;
        default:
            out->inputType = rcctl::InputType::Direct;
            break;
    }
    out->toggleRange = (parseIntArg("range_mode", fallbackRange) == 1) ? rcctl::ToggleRangeMode::Bipolar
                                                                        : rcctl::ToggleRangeMode::Unipolar;
    out->rumbleEnabled = parseBoolArg("rumble", fallbackRumble);
    const int fallbackModifier = out ? static_cast<int>(out->modifier) : static_cast<int>(InputId::None);
    const int fallbackFn = out ? static_cast<int>(out->modifierFunction) : 0;
    const int fallbackDz = out ? out->deadzonePercent : 10;
    const int fallbackExpo = out ? out->expoPercent : 0;
    out->modifier = static_cast<InputId>(parseIntArg("input_modifier", fallbackModifier));
    const int fn = parseIntArg("modifier_function", fallbackFn);
    switch (fn) {
        case 1:
            out->modifierFunction = rcctl::ModifierFunction::Reverse;
            break;
        case 2:
            out->modifierFunction = rcctl::ModifierFunction::Center;
            break;
        default:
            out->modifierFunction = rcctl::ModifierFunction::None;
            break;
    }
    out->deadzonePercent = constrain(parseIntArg("deadzone", fallbackDz), 0, 95);
    out->expoPercent = constrain(parseIntArg("expo", fallbackExpo), 0, 100);

    String name = g_server.hasArg("name") ? g_server.arg("name") : "Input";
    name.trim();
    if (name.isEmpty()) {
        name = existingIndex >= 0 ? "Input " + String(existingIndex) : "Input";
    }
    name.substring(0, sizeof(out->name) - 1).toCharArray(out->name, sizeof(out->name));
}

void parseOutputFromRequest(OutputChannelConfig* out, int existingIndex) {
    const int fallbackType = out ? static_cast<int>(out->type) : static_cast<int>(rcctl::ChannelType::Pwm);
    const int fallbackPin = out ? out->pin : 13;
    const int fallbackThreshold = out ? out->thresholdPercent : 50;
    const int fallbackSourceA = out ? out->sourceA : -1;
    const int fallbackSourceB = out ? out->sourceB : -1;
    const int fallbackSourceC = out ? out->sourceC : -1;
    const int fallbackMixMode = out ? static_cast<int>(out->mixMode) : 1;
    const int fallbackWeightA = out ? out->weightA : 100;
    const int fallbackWeightB = out ? out->weightB : 0;
    const int fallbackWeightC = out ? out->weightC : 0;
    const int fallbackOffsetA = out ? out->offsetA : 0;
    const int fallbackOffsetB = out ? out->offsetB : 0;
    const int fallbackOffsetC = out ? out->offsetC : 0;
    const bool fallbackInverted = out ? out->inverted : false;

    int typeValue = parseIntArg("type", fallbackType);
    out->type = (typeValue == static_cast<int>(rcctl::ChannelType::Switch)) ? rcctl::ChannelType::Switch
                                                                             : rcctl::ChannelType::Pwm;
    out->pin = static_cast<uint8_t>(constrain(parseIntArg("pin", fallbackPin), 0, 48));
    out->inverted = parseBoolArg("inverted", fallbackInverted);
    out->thresholdPercent = constrain(parseIntArg("threshold", fallbackThreshold), 0, 100);
    out->sourceA = static_cast<int8_t>(constrain(parseIntArg("source_a", fallbackSourceA), -1, kMaxVirtualInputs - 1));
    out->sourceB = static_cast<int8_t>(constrain(parseIntArg("source_b", fallbackSourceB), -1, kMaxVirtualInputs - 1));
    out->sourceC = static_cast<int8_t>(constrain(parseIntArg("source_c", fallbackSourceC), -1, kMaxVirtualInputs - 1));
    out->mixMode = (parseIntArg("mix_mode", fallbackMixMode) == 1) ? rcctl::MixMode::Multiply : rcctl::MixMode::Add;
    out->weightA = static_cast<int8_t>(constrain(parseIntArg("weight_a", fallbackWeightA), -100, 100));
    out->weightB = static_cast<int8_t>(constrain(parseIntArg("weight_b", fallbackWeightB), -100, 100));
    out->weightC = static_cast<int8_t>(constrain(parseIntArg("weight_c", fallbackWeightC), -100, 100));
    out->offsetA = static_cast<int8_t>(constrain(parseIntArg("offset_a", fallbackOffsetA), -100, 100));
    out->offsetB = static_cast<int8_t>(constrain(parseIntArg("offset_b", fallbackOffsetB), -100, 100));
    out->offsetC = static_cast<int8_t>(constrain(parseIntArg("offset_c", fallbackOffsetC), -100, 100));

    String name = g_server.hasArg("name") ? g_server.arg("name") : "Output";
    name.trim();
    if (name.isEmpty()) {
        name = existingIndex >= 0 ? "Output " + String(existingIndex) : "Output";
    }
    name.substring(0, sizeof(out->name) - 1).toCharArray(out->name, sizeof(out->name));
}

void handleApiState() {
    // "Static" state endpoint: returns full configuration + metadata for UI rebuild.
    PresetDirectory dir = {};
    rcctl::loadPresetDirectory(&dir);
    rcctl::StateSnapshot s = {
        .apIp = WiFi.softAPIP(),
        .apSsid = g_apSsid,
        .apPassword = g_apPassword,
        .gamepadConnected = (g_gamepad && g_gamepad->isConnected()),
        .btScanActive = g_btScanActive,
        .pairingEnabled = g_pairingScanEnabled,
        .currentModel = g_currentModelName,
        .bootModel = g_bootModelName,
        .modelDirty = g_modelDirty,
        .virtualInputs = g_virtualInputs,
        .outputs = g_outputs,
        .virtualRuntime = g_virtualRuntime,
        .outputRuntime = g_outputRuntime,
        .presetDir = &dir,
        .presetBuiltinA = kPresetRcCar,
        .presetBuiltinB = kPresetExcavator,
        .presetBuiltinC = kPresetSkidSteer,
    };
    g_server.send(200, "application/json", rcctl::buildStateJson(s));
}

void handleApiActivity() {
    // "Live" endpoint: lightweight payload polled frequently by the UI.
    rcctl::StateSnapshot s = {
        .apIp = IPAddress(0, 0, 0, 0),
        .apSsid = g_apSsid,
        .apPassword = "",
        .gamepadConnected = (g_gamepad && g_gamepad->isConnected()),
        .btScanActive = g_btScanActive,
        .pairingEnabled = g_pairingScanEnabled,
        .currentModel = g_currentModelName,
        .bootModel = g_bootModelName,
        .modelDirty = g_modelDirty,
        .virtualInputs = g_virtualInputs,
        .outputs = g_outputs,
        .virtualRuntime = g_virtualRuntime,
        .outputRuntime = g_outputRuntime,
        .presetDir = nullptr,
        .presetBuiltinA = "",
        .presetBuiltinB = "",
        .presetBuiltinC = "",
    };
    g_server.send(200, "application/json", rcctl::buildActivityJson(s));
}

void handleApiVirtualAdd() {
    int index = rcctl::firstFreeVirtualIndex();
    if (index < 0) {
        sendJson(false, "Input limit reached");
        return;
    }
    VirtualInputConfig draft;
    draft.used = true;
    parseVirtualFromRequest(&draft, index);
    g_virtualInputs[index] = draft;
    g_modelDirty = true;
    sendJson(true, "Input added");
}

void handleApiVirtualUpdate() {
    int index = parseIntArg("index", -1);
    if (index < 0 || index >= kMaxVirtualInputs || !g_virtualInputs[index].used) {
        sendJson(false, "Invalid input");
        return;
    }
    VirtualInputConfig draft = g_virtualInputs[index];
    parseVirtualFromRequest(&draft, index);
    draft.used = true;
    g_virtualInputs[index] = draft;
    g_modelDirty = true;
    sendJson(true, "Input updated");
}

void handleApiVirtualDelete() {
    int index = parseIntArg("index", -1);
    if (index < 0 || index >= kMaxVirtualInputs || !g_virtualInputs[index].used) {
        sendJson(false, "Invalid input");
        return;
    }
    g_virtualInputs[index] = VirtualInputConfig();
    for (int i = 0; i < kMaxOutputChannels; ++i) {
        if (!g_outputs[i].used) {
            continue;
        }
        if (g_outputs[i].sourceA == index) {
            g_outputs[i].sourceA = -1;
            g_outputs[i].weightA = 0;
            g_outputs[i].offsetA = 0;
        }
        if (g_outputs[i].sourceB == index) {
            g_outputs[i].sourceB = -1;
            g_outputs[i].weightB = 0;
            g_outputs[i].offsetB = 0;
        }
        if (g_outputs[i].sourceC == index) {
            g_outputs[i].sourceC = -1;
            g_outputs[i].weightC = 0;
            g_outputs[i].offsetC = 0;
        }
    }
    g_modelDirty = true;
    sendJson(true, "Input deleted");
}

void handleApiOutputAdd() {
    int index = rcctl::firstFreeOutputIndex();
    if (index < 0) {
        sendJson(false, "Output limit reached");
        return;
    }
    OutputChannelConfig draft;
    draft.used = true;
    parseOutputFromRequest(&draft, index);
    OutputChannelConfig previous = g_outputs[index];
    g_outputs[index] = draft;
    String error;
    if (!rcctl::rebuildOutputHardware(&error)) {
        g_outputs[index] = previous;
        String restoreError;
        rcctl::rebuildOutputHardware(&restoreError);
        sendJson(false, error);
        return;
    }
    g_modelDirty = true;
    sendJson(true, "Output added");
}

void handleApiOutputUpdate() {
    int index = parseIntArg("index", -1);
    if (index < 0 || index >= kMaxOutputChannels || !g_outputs[index].used) {
        sendJson(false, "Invalid output");
        return;
    }
    OutputChannelConfig backup = g_outputs[index];
    OutputChannelConfig draft = backup;
    parseOutputFromRequest(&draft, index);
    draft.used = true;
    const bool allowRewire = parseBoolArg("allow_rewire", false);
    if (!allowRewire) {
        // Protect against accidental remapping when user only tweaks simple fields (e.g. reverse).
        draft.type = backup.type;
        draft.pin = backup.pin;
        draft.sourceA = backup.sourceA;
        draft.sourceB = backup.sourceB;
        draft.sourceC = backup.sourceC;
        draft.mixMode = backup.mixMode;
        draft.weightA = backup.weightA;
        draft.weightB = backup.weightB;
        draft.weightC = backup.weightC;
        draft.offsetA = backup.offsetA;
        draft.offsetB = backup.offsetB;
        draft.offsetC = backup.offsetC;
    }
    g_outputs[index] = draft;
    String error;
    if (!rcctl::rebuildOutputHardware(&error)) {
        g_outputs[index] = backup;
        String restoreError;
        rcctl::rebuildOutputHardware(&restoreError);
        sendJson(false, error);
        return;
    }
    g_modelDirty = true;
    sendJson(true, "Output updated");
}

void handleApiOutputDelete() {
    int index = parseIntArg("index", -1);
    if (index < 0 || index >= kMaxOutputChannels || !g_outputs[index].used) {
        sendJson(false, "Invalid output");
        return;
    }
    OutputChannelConfig backup = g_outputs[index];
    g_outputs[index] = OutputChannelConfig();
    String error;
    if (!rcctl::rebuildOutputHardware(&error)) {
        g_outputs[index] = backup;
        String restoreError;
        rcctl::rebuildOutputHardware(&restoreError);
        sendJson(false, error);
        return;
    }
    g_modelDirty = true;
    sendJson(true, "Output deleted");
}

void handleApiPresetApply() {
    PersistedConfig cfg = {};
    const String modelName = rcctl::sanitizePresetName(g_server.arg("name"));
    String err;
    if (!rcctl::loadAnyPreset(modelName, &cfg, &err)) {
        sendJson(false, err);
        return;
    }
    if (!rcctl::applyPersistedConfig(cfg, &err)) {
        sendJson(false, err);
        return;
    }
    g_currentModelName = modelName;
    g_modelDirty = false;
    sendJson(true, "Preset applied");
}

void handleApiModelSetDefault() {
    const String modelName = rcctl::sanitizePresetName(g_server.arg("name"));
    PersistedConfig cfg = {};
    String err;
    if (!rcctl::loadAnyPreset(modelName, &cfg, &err)) {
        sendJson(false, err);
        return;
    }
    if (!rcctl::saveBootModelName(modelName)) {
        sendJson(false, "Failed to save boot model");
        return;
    }
    g_bootModelName = modelName;
    sendJson(true, "Boot model updated");
}

void handleApiModelCreate() {
    const String newName = rcctl::sanitizePresetName(g_server.arg("name"));
    if (newName.isEmpty()) {
        sendJson(false, "Invalid model name");
        return;
    }
    PersistedConfig cfg = {};
    cfg.magic = rcctl::kConfigMagic;
    cfg.version = rcctl::kConfigVersion;
    const bool fromCurrent = parseBoolArg("from_current", false);
    String rawBase = g_server.hasArg("base") ? g_server.arg("base") : "";
    rawBase.trim();
    const bool cloneEnabled = !rawBase.isEmpty() && !rawBase.equalsIgnoreCase("none");
    String err;
    if (fromCurrent) {
        rcctl::exportCurrentConfig(&cfg);
    } else if (cloneEnabled) {
        const String baseName = rcctl::sanitizePresetName(rawBase);
        if (!rcctl::loadAnyPreset(baseName, &cfg, &err)) {
            sendJson(false, err);
            return;
        }
    }
    if (!rcctl::saveUserPresetFromConfig(newName, cfg, &err)) {
        sendJson(false, err);
        return;
    }
    if (!rcctl::applyPersistedConfig(cfg, &err)) {
        sendJson(false, err);
        return;
    }
    g_currentModelName = newName;
    g_modelDirty = false;
    sendJson(true, "Model created");
}

void handleApiModelSaveCurrent() {
    if (g_currentModelName == kPresetRcCar || g_currentModelName == kPresetExcavator || g_currentModelName == kPresetSkidSteer) {
        const String suggested = rcctl::nextAvailableCustomModelName((g_currentModelName == kPresetRcCar) ? String("car_custom")
                                                     : ((g_currentModelName == kPresetExcavator) ? String("excavator_custom")
                                                                                                 : String("skid_steer_custom")));
        String json = "{\"ok\":false,\"readonly\":true,\"message\":\"Readonly preset: create a fork to save changes\","
                      "\"suggested_name\":\"";
        json += suggested;
        json += "\",\"base\":\"";
        json += g_currentModelName;
        json += "\"}";
        g_server.send(200, "application/json", json);
        return;
    }
    String err;
    if (!rcctl::saveCurrentAsUserPreset(g_currentModelName, &err)) {
        sendJson(false, err);
        return;
    }
    rcctl::saveRuntimeConfigToNvs();
    g_modelDirty = false;
    sendJson(true, "Model saved");
}

void handleApiModelRevert() {
    PersistedConfig cfg = {};
    String err;
    if (!rcctl::loadAnyPreset(g_currentModelName, &cfg, &err)) {
        sendJson(false, err);
        return;
    }
    if (!rcctl::applyPersistedConfig(cfg, &err)) {
        sendJson(false, err);
        return;
    }
    g_modelDirty = false;
    sendJson(true, "Changes reverted");
}

void handleApiModelDelete() {
    const String modelName = rcctl::sanitizePresetName(g_server.arg("name"));
    if (modelName == kPresetRcCar || modelName == kPresetExcavator || modelName == kPresetSkidSteer) {
        sendJson(false, "Readonly preset");
        return;
    }
    String err;
    if (!rcctl::deleteUserPreset(modelName, &err)) {
        sendJson(false, err);
        return;
    }

    if (g_bootModelName == modelName) {
        g_bootModelName = String(kPresetRcCar);
        rcctl::saveBootModelName(g_bootModelName);
    }

    if (g_currentModelName == modelName) {
        PersistedConfig cfg = {};
        if (!rcctl::loadAnyPreset(g_bootModelName, &cfg, &err)) {
            g_bootModelName = String(kPresetRcCar);
            g_currentModelName = g_bootModelName;
            rcctl::loadAnyPreset(g_bootModelName, &cfg, &err);
        } else {
            g_currentModelName = g_bootModelName;
        }
        rcctl::applyPersistedConfig(cfg, &err);
        g_modelDirty = false;
    }
    sendJson(true, "Preset deleted");
}

void handleApiApConfigSet() {
    if (!g_server.hasArg("ssid") || !g_server.hasArg("password")) {
        sendJson(false, "Missing AP fields");
        return;
    }
    const String nextSsid = sanitizeApSsid(g_server.arg("ssid"));
    const String nextPass = sanitizeApPassword(g_server.arg("password"));
    if (!isValidApPassword(nextPass)) {
        sendJson(false, "Password must be empty (open) or 8-63 chars");
        return;
    }
    if (!saveApSsid(nextSsid) || !saveApPassword(nextPass)) {
        sendJson(false, "Failed to save AP config");
        return;
    }
    g_apSsid = nextSsid;
    g_apPassword = nextPass;
    sendJson(true, "AP config saved. Reboot required to apply.");
}

void handleApiApConfigApplyReboot() {
    if (!g_server.hasArg("ssid") || !g_server.hasArg("password")) {
        sendJson(false, "Missing AP fields");
        return;
    }
    const String nextSsid = sanitizeApSsid(g_server.arg("ssid"));
    const String nextPass = sanitizeApPassword(g_server.arg("password"));
    if (!isValidApPassword(nextPass)) {
        sendJson(false, "Password must be empty (open) or 8-63 chars");
        return;
    }
    if (!saveApSsid(nextSsid) || !saveApPassword(nextPass)) {
        sendJson(false, "Failed to save AP config");
        return;
    }
    g_apSsid = nextSsid;
    g_apPassword = nextPass;
    sendJson(true, "AP config saved. Rebooting now...");
    delay(250);
    ESP.restart();
}

void handleApiPairingOn() {
    g_pairingScanEnabled = true;
    g_pairingScanUntilMs = millis() + kPairingWindowMs;
    g_btScanToggleAtMs = 0;
    sendJson(true, "Pairing ON");
}

void handleApiPairingOff() {
    g_pairingScanEnabled = false;
    g_pairingScanUntilMs = 0;
    g_btScanToggleAtMs = 0;
    sendJson(true, "Pairing OFF");
}

void handleApiLearnDetect() {
    if (!g_gamepad || !g_gamepad->isConnected()) {
        g_server.send(400, "application/json", "{\"ok\":false,\"message\":\"No gamepad connected\"}");
        return;
    }
    const InputId hit = detectDominantInput(g_gamepad);
    if (hit == InputId::None) {
        g_server.send(200, "application/json", "{\"ok\":false,\"message\":\"No active input\"}");
        return;
    }
    const InputDefinition* def = getInputDefinition(hit);
    String json = "{\"ok\":true,\"input\":";
    json += String(static_cast<int>(hit));
    json += ",\"label\":\"";
    json += def ? String(def->label) : String("Unknown");
    json += "\"}";
    g_server.send(200, "application/json", json);
}

void setupWebUi() {
    WiFi.setSleep(false);
    WiFi.mode(WIFI_AP);
    const bool apOpen = g_apPassword.length() < 8;
    WiFi.softAP(g_apSsid.c_str(), apOpen ? nullptr : g_apPassword.c_str(), kApChannel, false, kApMaxConnections);

    wifi_config_t apCfg = {};
    if (esp_wifi_get_config(WIFI_IF_AP, &apCfg) == ESP_OK) {
        apCfg.ap.channel = kApChannel;
        apCfg.ap.max_connection = kApMaxConnections;
        apCfg.ap.ssid_hidden = 0;
        if (apOpen) {
            apCfg.ap.authmode = WIFI_AUTH_OPEN;
            memset(apCfg.ap.password, 0, sizeof(apCfg.ap.password));
            apCfg.ap.pmf_cfg.capable = false;
            apCfg.ap.pmf_cfg.required = false;
        } else {
            apCfg.ap.authmode = WIFI_AUTH_WPA2_PSK;
            strncpy(reinterpret_cast<char*>(apCfg.ap.password), g_apPassword.c_str(), sizeof(apCfg.ap.password) - 1);
            apCfg.ap.password[sizeof(apCfg.ap.password) - 1] = '\0';
            apCfg.ap.pmf_cfg.capable = true;
            apCfg.ap.pmf_cfg.required = false;
        }
        esp_wifi_set_config(WIFI_IF_AP, &apCfg);
        esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
        esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G);
    }

    rcctl::WebRouteHandlers routes = {};
    routes.root = handleRoot;
    routes.captiveRedirect = redirectCaptivePortal;
    routes.apiState = handleApiState;
    routes.apiActivity = handleApiActivity;
    routes.apiInputs = handleApiInputs;
    routes.apiVirtualAdd = handleApiVirtualAdd;
    routes.apiVirtualUpdate = handleApiVirtualUpdate;
    routes.apiVirtualDelete = handleApiVirtualDelete;
    routes.apiOutputAdd = handleApiOutputAdd;
    routes.apiOutputUpdate = handleApiOutputUpdate;
    routes.apiOutputDelete = handleApiOutputDelete;
    routes.apiPresetApply = handleApiPresetApply;
    routes.apiModelSetDefault = handleApiModelSetDefault;
    routes.apiModelCreate = handleApiModelCreate;
    routes.apiModelSaveCurrent = handleApiModelSaveCurrent;
    routes.apiModelRevert = handleApiModelRevert;
    routes.apiModelDelete = handleApiModelDelete;
    routes.apiApConfigSet = handleApiApConfigSet;
    routes.apiApConfigApplyReboot = handleApiApConfigApplyReboot;
    routes.apiPairingOn = handleApiPairingOn;
    routes.apiPairingOff = handleApiPairingOff;
    routes.apiLearnDetect = handleApiLearnDetect;
    routes.notFound = []() {
        if (g_server.method() == HTTP_GET) {
            redirectCaptivePortal();
            return;
        }
        g_server.send(404, "text/plain", "Not found");
    };
    rcctl::registerWebRoutes(g_server, routes);

    g_server.begin();
    g_dnsServer.start(53, "*", WiFi.softAPIP());
}

void onConnectedController(ControllerPtr ctl) {
    if (!ctl || ctl->isMouse() || ctl->isKeyboard() || ctl->isBalanceBoard()) {
        return;
    }
    if (g_gamepad && g_gamepad->isConnected()) {
        return;
    }
    g_gamepad = ctl;
    g_lastPacketMs = millis();
    g_signalTimedOut = false;
    if (g_pairingScanEnabled) {
        g_pairingScanEnabled = false;
        g_pairingScanUntilMs = 0;
    }
    g_btScanToggleAtMs = 0;
    setBtScanActive(false, "controller connected");
}

void onDisconnectedController(ControllerPtr ctl) {
    if (ctl == g_gamepad) {
        g_gamepad = nullptr;
        g_signalTimedOut = false;
        rcctl::applyFailsafeAllOutputs();
        g_btScanToggleAtMs = 0;
    }
}

}  // namespace

void setup() {
#if RCCTL_DEBUG_LOG
    Console.printf("Firmware: %s\n", BP32.firmwareVersion());
    const uint8_t* addr = BP32.localBdAddress();
    Console.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
#endif

    esp_err_t nvsErr = nvs_flash_init();
    if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvsErr = nvs_flash_init();
    }

    g_apSsid = loadApSsid();
    g_apPassword = loadApPassword();
    esp_coex_preference_set(ESP_COEX_PREFER_WIFI);

    if (!kDiagDisableBluepad32) {
        BP32.setup(&onConnectedController, &onDisconnectedController, false);
        BP32.enableVirtualDevice(false);
        BP32.enableBLEService(false);
        setBtScanActive(false, "startup");
        g_pairingScanEnabled = false;
        g_pairingScanUntilMs = 0;
        g_btScanToggleAtMs = millis() + 5000;
    }

    g_bootModelName = rcctl::loadBootModelName();
    g_currentModelName = g_bootModelName;
    PersistedConfig cfg = {};
    String err;
    if (!rcctl::loadAnyPreset(g_bootModelName, &cfg, &err)) {
        g_bootModelName = String(kPresetRcCar);
        g_currentModelName = g_bootModelName;
        rcctl::loadAnyPreset(g_bootModelName, &cfg, &err);
    }
    rcctl::applyPersistedConfig(cfg, &err);
    g_modelDirty = false;

    // Best-effort mount: streamWebUiPage() retries and returns explicit HTTP error if missing.
    initWebUiStorage();
    setupWebUi();
    rcctl::applyFailsafeAllOutputs();
}

void loop() {
    if (!kDiagDisableBluepad32) {
        BP32.update();
        updateBtScanScheduler();
    }

    g_dnsServer.processNextRequest();
    g_server.handleClient();

    const uint32_t now = millis();
    rcctl::processControlTick(g_gamepad, now, kSignalTimeoutMs, &g_lastPacketMs, &g_signalTimedOut);
    if (now - g_lastIoTraceMs > kIoTraceMs) {
        g_lastIoTraceMs = now;
        emitIoTrace(g_gamepad);
    }

    if (RCCTL_DEBUG_LOG && now - g_lastUiLogMs > kUiRefreshMs) {
        g_lastUiLogMs = now;
        const int active = rcctl::countActiveOutputs(g_outputs);
        Console.printf("Status: outputs=%d gamepad=%s\n", active, (g_gamepad && g_gamepad->isConnected()) ? "ON" : "OFF");
    }

    delay(20);
}
