// SPDX-License-Identifier: Apache-2.0

#include "sdkconfig.h"

#include <Arduino.h>
#include <Bluepad32.h>
#include <DNSServer.h>
#include <ESP32Servo.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_coexist.h>

#include "control_inputs.h"
#include "preset_store.h"
#include "web_ui.h"

#include <cstring>
#include <math.h>
#include <nvs.h>
#include <nvs_flash.h>

namespace {

using rcctl::kConfigMagic;
using rcctl::kConfigVersion;
using rcctl::kMaxChannels;
using rcctl::kPresetDirectoryVersion;
using rcctl::kPresetExcavator;
using rcctl::kPresetRcCar;
using rcctl::ChannelType;
using rcctl::PersistedConfig;
using rcctl::PresetDirectory;

constexpr int kPwmMinUs = 1000;
constexpr int kPwmNeutralUs = 1500;
constexpr int kPwmMaxUs = 2000;

constexpr uint32_t kSignalTimeoutMs = 450;
constexpr uint32_t kUiRefreshMs = 2500;

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
constexpr uint32_t kLearnTimeoutMs = 12000;

struct ChannelConfig {
    bool used = false;
    ChannelType type = ChannelType::Pwm;
    uint8_t pin = 0;
    bool inverted = false;
    int switchThresholdPercent = 50;
    InputId input = InputId::None;
    InputId inputSecondary = InputId::None;
    InputId inputModifier = InputId::None;
    bool modifierReverses = false;
    char name[24] = "";
};

ChannelConfig g_channels[kMaxChannels];
Servo g_pwmOutputs[kMaxChannels];
bool g_pwmAttached[kMaxChannels] = {false};

ControllerPtr g_gamepad = nullptr;
uint32_t g_lastPacketMs = 0;
uint32_t g_lastUiLogMs = 0;
bool g_signalTimedOut = false;
bool g_pairingScanEnabled = false;
uint32_t g_pairingScanUntilMs = 0;
bool g_btScanActive = false;
uint32_t g_btScanToggleAtMs = 0;
int g_learningChannelIndex = -1;
InputId g_learningCandidate = InputId::None;
uint8_t g_learningCandidateHits = 0;
uint32_t g_learningDeadlineMs = 0;
String g_currentModelName = kPresetRcCar;
String g_bootModelName = kPresetRcCar;
String g_apSsid = kDefaultApSsid;
String g_apPassword = kDefaultApPassword;
bool g_modelDirty = false;

WebServer g_server(80);
DNSServer g_dnsServer;

int clampPwmUs(int us) {
    return constrain(us, kPwmMinUs, kPwmMaxUs);
}

int applyPwmInvert(int us, bool inverted) {
    if (!inverted) {
        return us;
    }
    return clampPwmUs(kPwmMinUs + kPwmMaxUs - us);
}

float applyDeadzoneSigned(float v, int deadzonePercent) {
    const float dz = constrain(deadzonePercent, 0, 95) / 100.0f;
    const float a = fabsf(v);
    if (a <= dz) {
        return 0.0f;
    }
    const float scaled = (a - dz) / (1.0f - dz);
    return v < 0.0f ? -scaled : scaled;
}

int pwmFromInput(const ChannelConfig& ch, ControllerPtr ctl) {
    const InputDefinition* def = getInputDefinition(ch.input);
    if (!def) {
        return kPwmNeutralUs;
    }

    float n = normalizedForInput(ch.input, ctl);
    if (ch.inputSecondary != InputId::None) {
        const float s = normalizedForInput(ch.inputSecondary, ctl);
        n = applyDeadzoneSigned(constrain(n - s, -1.0f, 1.0f), ch.switchThresholdPercent);
    } else if (def->kind == InputKind::Centered) {
        n = applyDeadzoneSigned(n, ch.switchThresholdPercent);
    }
    if (ch.modifierReverses && isInputActive(ch.inputModifier, ctl)) {
        n = -n;
    }
    int us = kPwmNeutralUs;

    if (def->kind == InputKind::Centered) {
        us = static_cast<int>(lroundf(kPwmNeutralUs + n * static_cast<float>(kPwmMaxUs - kPwmNeutralUs)));
    } else {
        us = static_cast<int>(lroundf(kPwmNeutralUs + n * static_cast<float>(kPwmMaxUs - kPwmNeutralUs)));
    }

    return applyPwmInvert(clampPwmUs(us), ch.inverted);
}

float channelSignedActivity(const ChannelConfig& ch, ControllerPtr ctl) {
    if (!ctl || !ctl->isConnected()) {
        return 0.0f;
    }
    const InputDefinition* def = getInputDefinition(ch.input);
    if (!def) {
        return 0.0f;
    }
    float n = normalizedForInput(ch.input, ctl);
    if (ch.inputSecondary != InputId::None) {
        n = applyDeadzoneSigned(constrain(n - normalizedForInput(ch.inputSecondary, ctl), -1.0f, 1.0f),
                                ch.switchThresholdPercent);
    } else if (def->kind == InputKind::Positive || def->kind == InputKind::Digital) {
        // Map [0..1] style inputs to signed for a centered visual gauge.
        n = constrain(n, 0.0f, 1.0f);
    } else {
        n = applyDeadzoneSigned(n, ch.switchThresholdPercent);
    }
    if (ch.modifierReverses && isInputActive(ch.inputModifier, ctl)) {
        n = -n;
    }
    return constrain(n, -1.0f, 1.0f);
}

int switchFromInput(const ChannelConfig& ch, ControllerPtr ctl) {
    const InputDefinition* def = getInputDefinition(ch.input);
    if (!def) {
        return LOW;
    }

    const float n = normalizedForInput(ch.input, ctl);
    float analog01 = n;
    if (def->kind == InputKind::Centered) {
        analog01 = (n + 1.0f) * 0.5f;
    }

    bool on = false;
    if (def->kind == InputKind::Digital) {
        on = n >= 0.5f;
    } else {
        const float threshold = constrain(ch.switchThresholdPercent, 0, 100) / 100.0f;
        on = analog01 >= threshold;
    }

    if (ch.inverted) {
        on = !on;
    }
    return on ? HIGH : LOW;
}

void writeFailsafeForChannel(int index) {
    if (!g_channels[index].used) {
        return;
    }
    if (g_channels[index].type == ChannelType::Pwm) {
        if (g_pwmAttached[index]) {
            g_pwmOutputs[index].writeMicroseconds(kPwmNeutralUs);
        }
    } else {
        pinMode(g_channels[index].pin, OUTPUT);
        digitalWrite(g_channels[index].pin, LOW);
    }
}

void applyFailsafeAllChannels() {
    for (int i = 0; i < kMaxChannels; ++i) {
        writeFailsafeForChannel(i);
    }
}

bool pinAlreadyUsed(uint8_t pin, int ignoreIndex) {
    for (int i = 0; i < kMaxChannels; ++i) {
        if (i == ignoreIndex) {
            continue;
        }
        if (g_channels[i].used && g_channels[i].pin == pin) {
            return true;
        }
    }
    return false;
}

void releaseChannelHardware(int index) {
    if (g_pwmAttached[index]) {
        g_pwmOutputs[index].detach();
        g_pwmAttached[index] = false;
    }
}

bool setupChannelHardware(int index, String* error) {
    const ChannelConfig& ch = g_channels[index];

    if (pinAlreadyUsed(ch.pin, index)) {
        if (error) {
            *error = "Pin deja utilise";
        }
        return false;
    }

    if (ch.type == ChannelType::Pwm) {
        g_pwmOutputs[index].setPeriodHertz(50);
        int attached = g_pwmOutputs[index].attach(ch.pin, kPwmMinUs, kPwmMaxUs);
        if (attached <= 0) {
            if (error) {
                *error = "Echec attach PWM";
            }
            return false;
        }
        g_pwmAttached[index] = true;
    } else {
        pinMode(ch.pin, OUTPUT);
        digitalWrite(ch.pin, LOW);
    }

    writeFailsafeForChannel(index);
    return true;
}

bool saveConfigToNvs() {
    PersistedConfig cfg = {};
    cfg.magic = kConfigMagic;
    cfg.version = kConfigVersion;

    for (int i = 0; i < kMaxChannels; ++i) {
        cfg.channels[i].used = g_channels[i].used ? 1 : 0;
        cfg.channels[i].type = static_cast<uint8_t>(g_channels[i].type);
        cfg.channels[i].pin = g_channels[i].pin;
        cfg.channels[i].inverted = g_channels[i].inverted ? 1 : 0;
        cfg.channels[i].input = static_cast<uint8_t>(g_channels[i].input);
        cfg.channels[i].input_secondary = static_cast<uint8_t>(g_channels[i].inputSecondary);
        cfg.channels[i].input_modifier = static_cast<uint8_t>(g_channels[i].inputModifier);
        cfg.channels[i].modifier_reverses = g_channels[i].modifierReverses ? 1 : 0;
        cfg.channels[i].threshold = static_cast<uint8_t>(constrain(g_channels[i].switchThresholdPercent, 0, 100));
        memcpy(cfg.channels[i].name, g_channels[i].name, sizeof(cfg.channels[i].name));
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        Console.printf("NVS: open ecriture KO (%d)\n", static_cast<int>(err));
        return false;
    }

    err = nvs_set_blob(handle, "cfg", &cfg, sizeof(cfg));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    const bool ok = err == ESP_OK;
    Console.printf("NVS: sauvegarde config %s\n", ok ? "OK" : "ECHEC");
    return ok;
}

void loadConfigFromNvs() {
    PersistedConfig cfg = {};

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        Console.printf("NVS: open lecture KO (%d)\n", static_cast<int>(err));
        return;
    }

    size_t len = sizeof(cfg);
    err = nvs_get_blob(handle, "cfg", &cfg, &len);
    nvs_close(handle);

    if (err != ESP_OK || len != sizeof(cfg)) {
        Console.println("NVS: aucune config valide sauvegardee");
        return;
    }

    if (cfg.magic != kConfigMagic || cfg.version != kConfigVersion) {
        Console.println("NVS: format de config invalide");
        return;
    }

    for (int i = 0; i < kMaxChannels; ++i) {
        g_channels[i] = ChannelConfig();

        if (!cfg.channels[i].used) {
            continue;
        }

        g_channels[i].used = true;
        g_channels[i].type = (cfg.channels[i].type == static_cast<uint8_t>(ChannelType::Switch)) ? ChannelType::Switch
                                                                                                  : ChannelType::Pwm;
        g_channels[i].pin = cfg.channels[i].pin;
        g_channels[i].inverted = cfg.channels[i].inverted != 0;
        g_channels[i].input = static_cast<InputId>(cfg.channels[i].input);
        g_channels[i].inputSecondary = static_cast<InputId>(cfg.channels[i].input_secondary);
        g_channels[i].inputModifier = static_cast<InputId>(cfg.channels[i].input_modifier);
        g_channels[i].modifierReverses = cfg.channels[i].modifier_reverses != 0;
        g_channels[i].switchThresholdPercent = constrain(cfg.channels[i].threshold, 0, 100);
        memcpy(g_channels[i].name, cfg.channels[i].name, sizeof(g_channels[i].name));
        g_channels[i].name[sizeof(g_channels[i].name) - 1] = '\0';

        String error;
        if (!setupChannelHardware(i, &error)) {
            Console.printf("NVS: channel %d ignore (%s)\n", i, error.c_str());
            g_channels[i] = ChannelConfig();
        }
    }

    Console.println("NVS: configuration chargee");
}

void processGamepadToChannels(ControllerPtr ctl) {
    for (int i = 0; i < kMaxChannels; ++i) {
        if (!g_channels[i].used) {
            continue;
        }

        if (g_channels[i].type == ChannelType::Pwm) {
            if (g_pwmAttached[i]) {
                g_pwmOutputs[i].writeMicroseconds(pwmFromInput(g_channels[i], ctl));
            }
        } else {
            digitalWrite(g_channels[i].pin, switchFromInput(g_channels[i], ctl));
        }
    }
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
    Console.printf("BT scan: %s (%s)\n", enabled ? "ON" : "OFF", reason);
}

void updateBtScanScheduler() {
    if (kDiagDisableBluepad32) {
        return;
    }

    const uint32_t now = millis();
    const bool connected = g_gamepad && g_gamepad->isConnected();

    if (g_pairingScanEnabled && now > g_pairingScanUntilMs) {
        g_pairingScanEnabled = false;
        Console.println("BT pairing mode: timeout -> OFF");
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

bool exportCurrentConfig(PersistedConfig* out) {
    if (!out) {
        return false;
    }
    PersistedConfig cfg = {};
    cfg.magic = kConfigMagic;
    cfg.version = kConfigVersion;
    for (int i = 0; i < kMaxChannels; ++i) {
        cfg.channels[i].used = g_channels[i].used ? 1 : 0;
        cfg.channels[i].type = static_cast<uint8_t>(g_channels[i].type);
        cfg.channels[i].pin = g_channels[i].pin;
        cfg.channels[i].inverted = g_channels[i].inverted ? 1 : 0;
        cfg.channels[i].input = static_cast<uint8_t>(g_channels[i].input);
        cfg.channels[i].input_secondary = static_cast<uint8_t>(g_channels[i].inputSecondary);
        cfg.channels[i].input_modifier = static_cast<uint8_t>(g_channels[i].inputModifier);
        cfg.channels[i].modifier_reverses = g_channels[i].modifierReverses ? 1 : 0;
        cfg.channels[i].threshold = static_cast<uint8_t>(constrain(g_channels[i].switchThresholdPercent, 0, 100));
        memcpy(cfg.channels[i].name, g_channels[i].name, sizeof(cfg.channels[i].name));
    }
    *out = cfg;
    return true;
}

bool applyPersistedConfig(const PersistedConfig& cfg, String* errorOut = nullptr) {
    if (cfg.magic != kConfigMagic || cfg.version != kConfigVersion) {
        if (errorOut) {
            *errorOut = "Format preset invalide";
        }
        return false;
    }

    ChannelConfig previous[kMaxChannels];
    bool previousPwm[kMaxChannels];
    for (int i = 0; i < kMaxChannels; ++i) {
        previous[i] = g_channels[i];
        previousPwm[i] = g_pwmAttached[i];
        if (g_channels[i].used) {
            releaseChannelHardware(i);
        }
        g_channels[i] = ChannelConfig();
    }

    for (int i = 0; i < kMaxChannels; ++i) {
        if (!cfg.channels[i].used) {
            continue;
        }
        g_channels[i].used = true;
        g_channels[i].type = (cfg.channels[i].type == static_cast<uint8_t>(ChannelType::Switch)) ? ChannelType::Switch
                                                                                                   : ChannelType::Pwm;
        g_channels[i].pin = cfg.channels[i].pin;
        g_channels[i].inverted = cfg.channels[i].inverted != 0;
        g_channels[i].input = static_cast<InputId>(cfg.channels[i].input);
        g_channels[i].inputSecondary = static_cast<InputId>(cfg.channels[i].input_secondary);
        g_channels[i].inputModifier = static_cast<InputId>(cfg.channels[i].input_modifier);
        g_channels[i].modifierReverses = cfg.channels[i].modifier_reverses != 0;
        g_channels[i].switchThresholdPercent = constrain(cfg.channels[i].threshold, 0, 100);
        memcpy(g_channels[i].name, cfg.channels[i].name, sizeof(g_channels[i].name));
        g_channels[i].name[sizeof(g_channels[i].name) - 1] = '\0';

        String error;
        if (!setupChannelHardware(i, &error)) {
            for (int k = 0; k < kMaxChannels; ++k) {
                if (g_channels[k].used) {
                    releaseChannelHardware(k);
                }
                g_channels[k] = previous[k];
                g_pwmAttached[k] = false;
                if (g_channels[k].used && previousPwm[k]) {
                    String restoreError;
                    setupChannelHardware(k, &restoreError);
                }
            }
            if (errorOut) {
                *errorOut = "Preset invalide: " + error;
            }
            return false;
        }
    }

    applyFailsafeAllChannels();
    saveConfigToNvs();
    return true;
}

bool saveUserPreset(const String& rawName, const PersistedConfig* sourceCfg, String* errorOut = nullptr) {
    return rcctl::saveUserPresetBlob(rawName, sourceCfg, errorOut);
}

bool saveUserPreset(const String& rawName, String* errorOut = nullptr) {
    PersistedConfig cfg = {};
    exportCurrentConfig(&cfg);
    return rcctl::saveUserPresetBlob(rawName, &cfg, errorOut);
}

bool presetNameExists(const String& rawName) {
    rcctl::PresetDirectory dir = {};
    rcctl::loadPresetDirectory(&dir);
    const String target = rcctl::sanitizePresetName(rawName);
    for (int i = 0; i < static_cast<int>(dir.count); ++i) {
        if (target.equalsIgnoreCase(String(dir.names[i]))) {
            return true;
        }
    }
    return false;
}

String nextAvailableCustomModelName(const String& baseName) {
    const String base = rcctl::sanitizePresetName(baseName);
    if (!presetNameExists(base)) {
        return base;
    }
    for (int i = 2; i <= 99; ++i) {
        String candidate = base + "_" + String(i);
        if (!presetNameExists(candidate)) {
            return candidate;
        }
    }
    return base + "_" + String(millis() % 1000);
}

bool hasAnyConfiguredChannel() {
    for (int i = 0; i < kMaxChannels; ++i) {
        if (g_channels[i].used) {
            return true;
        }
    }
    return false;
}

String htmlHeader(const char* title) {
    String s;
    s += "<!doctype html><html><head><meta charset='utf-8'>";
    s += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
    s += "<meta http-equiv='refresh' content='";
    s += String(kUiRefreshMs / 1000);
    s += "'>";
    s += "<title>";
    s += title;
    s += "</title>";
    s += "<style>";
    s += "body{font-family:Arial,sans-serif;margin:12px;background:#f3f4f6;color:#111827;}";
    s += "h1{font-size:1.2rem;margin:0 0 8px 0;}";
    s += "h2{font-size:1rem;margin:12px 0 8px 0;}";
    s += ".card{background:#fff;border:1px solid #d1d5db;border-radius:8px;padding:10px;margin-bottom:10px;}";
    s += "label{display:block;font-size:0.85rem;margin-top:6px;}";
    s += "input,select{width:100%;padding:8px;margin-top:2px;border:1px solid #9ca3af;border-radius:6px;box-sizing:border-box;}";
    s += "button{margin-top:8px;padding:8px 10px;border:0;border-radius:6px;background:#2563eb;color:#fff;}";
    s += "button.danger{background:#b91c1c;}";
    s += "small{color:#4b5563;}";
    s += "</style></head><body>";
    return s;
}

String channelTypeToString(ChannelType type) {
    return type == ChannelType::Pwm ? "PWM" : "ON/OFF";
}

int parseIntArg(const String& name, int fallback) {
    if (!g_server.hasArg(name)) {
        return fallback;
    }
    return g_server.arg(name).toInt();
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
        return true;  // open AP
    }
    return pass.length() >= 8 && pass.length() <= 63;
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
    String json = "{\"ok\":";
    json += ok ? "true" : "false";
    if (message.length()) {
        json += ",\"message\":\"";
        for (size_t i = 0; i < message.length(); ++i) {
            const char c = message[i];
            if (c == '"' || c == '\\') {
                json += '\\';
            }
            if (c == '\n') {
                json += "\\n";
            } else {
                json += c;
            }
        }
        json += "\"";
    }
    json += "}";
    g_server.send(ok ? 200 : 400, "application/json", json);
}

void handleRoot() {
    String inputsJson = "[";
    bool firstInput = true;
    for (size_t i = 0; i < kInputCount; ++i) {
        if (kInputs[i].id == InputId::ButtonL2 || kInputs[i].id == InputId::ButtonR2) {
            continue;
        }
        if (!firstInput) {
            inputsJson += ",";
        }
        firstInput = false;
        String label = String(kInputs[i].label);
        label.replace("\\", "\\\\");
        label.replace("\"", "\\\"");
        inputsJson += "{\"id\":";
        inputsJson += String(static_cast<int>(kInputs[i].id));
        inputsJson += ",\"label\":\"";
        inputsJson += label;
        inputsJson += "\"}";
    }
    inputsJson += "]";
    const String html = buildWebUiPage(inputsJson);
    g_server.send(200, "text/html", html);
}

void redirectToRoot() {
    g_server.sendHeader("Location", "/", true);
    g_server.send(303, "text/plain", "");
}

void redirectCaptivePortal() {
    g_server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    g_server.sendHeader("Pragma", "no-cache");
    g_server.sendHeader("Expires", "-1");
    g_server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    g_server.send(302, "text/plain", "");
}

int firstFreeChannelIndex() {
    for (int i = 0; i < kMaxChannels; ++i) {
        if (!g_channels[i].used) {
            return i;
        }
    }
    return -1;
}

void parseChannelFromRequest(ChannelConfig* out, int existingIndex) {
    int typeValue = parseIntArg("type", static_cast<int>(ChannelType::Pwm));
    out->type = (typeValue == static_cast<int>(ChannelType::Switch)) ? ChannelType::Switch : ChannelType::Pwm;
    out->pin = static_cast<uint8_t>(constrain(parseIntArg("pin", 13), 0, 48));
    out->input = static_cast<InputId>(parseIntArg("input", static_cast<int>(InputId::AxisX)));
    out->inputSecondary = static_cast<InputId>(parseIntArg("input_secondary", static_cast<int>(InputId::None)));
    out->inputModifier = static_cast<InputId>(parseIntArg("input_modifier", static_cast<int>(InputId::None)));
    out->modifierReverses = parseBoolArg("modifier_reverses", false);
    out->inverted = parseBoolArg("inverted", false);
    out->switchThresholdPercent = constrain(parseIntArg("threshold", 50), 0, 100);

    String name = g_server.hasArg("name") ? g_server.arg("name") : "CH";
    name.trim();
    if (name.isEmpty()) {
        if (existingIndex >= 0) {
            name = "CH" + String(existingIndex);
        } else {
            name = "CH";
        }
    }
    name.substring(0, sizeof(out->name) - 1).toCharArray(out->name, sizeof(out->name));
}

void handleAddChannel() {
    int index = firstFreeChannelIndex();
    if (index < 0) {
        g_server.send(400, "text/plain", "Limite de channels atteinte");
        return;
    }

    ChannelConfig draft;
    draft.used = true;
    parseChannelFromRequest(&draft, index);

    g_channels[index] = draft;

    String error;
    if (!setupChannelHardware(index, &error)) {
        g_channels[index].used = false;
        g_server.send(400, "text/plain", error);
        return;
    }

    saveConfigToNvs();
    redirectToRoot();
}

void handleUpdateChannel() {
    int index = parseIntArg("index", -1);
    if (index < 0 || index >= kMaxChannels || !g_channels[index].used) {
        g_server.send(400, "text/plain", "Channel invalide");
        return;
    }

    ChannelConfig backup = g_channels[index];
    releaseChannelHardware(index);

    ChannelConfig draft = backup;
    parseChannelFromRequest(&draft, index);
    draft.used = true;
    g_channels[index] = draft;

    String error;
    if (!setupChannelHardware(index, &error)) {
        g_channels[index] = backup;
        String restoreError;
        setupChannelHardware(index, &restoreError);
        g_server.send(400, "text/plain", error);
        return;
    }

    saveConfigToNvs();
    redirectToRoot();
}

void handleDeleteChannel() {
    int index = parseIntArg("index", -1);
    if (index < 0 || index >= kMaxChannels || !g_channels[index].used) {
        g_server.send(400, "text/plain", "Channel invalide");
        return;
    }

    releaseChannelHardware(index);
    g_channels[index] = ChannelConfig();
    saveConfigToNvs();
    redirectToRoot();
}

void handleClearChannels() {
    for (int i = 0; i < kMaxChannels; ++i) {
        if (!g_channels[i].used) {
            continue;
        }
        releaseChannelHardware(i);
        g_channels[i] = ChannelConfig();
    }
    saveConfigToNvs();
    redirectToRoot();
}

float channelActivity01(const ChannelConfig& ch, ControllerPtr ctl) {
    const InputDefinition* def = getInputDefinition(ch.input);
    if (!def || !ctl || !ctl->isConnected()) {
        return 0.0f;
    }
    float n = normalizedForInput(ch.input, ctl);
    if (def->kind == InputKind::Digital) {
        return n >= 0.5f ? 1.0f : 0.0f;
    }
    if (def->kind == InputKind::Centered) {
        n = fabsf(n);
    }
    return constrain(n, 0.0f, 1.0f);
}

void handleApiState() {
    String json = "{";
    String apSsidJson = g_apSsid;
    apSsidJson.replace("\\", "\\\\");
    apSsidJson.replace("\"", "\\\"");
    String apPassJson = g_apPassword;
    apPassJson.replace("\\", "\\\\");
    apPassJson.replace("\"", "\\\"");
    json += "\"ap_ip\":\"" + WiFi.softAPIP().toString() + "\",";
    json += "\"ap_ssid\":\"" + apSsidJson + "\",";
    json += "\"ap_password\":\"" + apPassJson + "\",";
    json += "\"gamepad\":";
    json += (g_gamepad && g_gamepad->isConnected()) ? "true" : "false";
    json += ",\"bt_scan\":";
    json += g_btScanActive ? "true" : "false";
    json += ",\"pairing\":";
    json += g_pairingScanEnabled ? "true" : "false";
    json += ",\"learning_index\":";
    json += String(g_learningChannelIndex);
    json += ",\"current_model\":\"";
    json += g_currentModelName;
    json += "\",\"boot_model\":\"";
    json += g_bootModelName;
    json += "\",\"model_dirty\":";
    json += g_modelDirty ? "true" : "false";
    json += ",\"channels\":[";
    bool first = true;
    for (int i = 0; i < kMaxChannels; ++i) {
        if (!g_channels[i].used) {
            continue;
        }
        if (!first) {
            json += ",";
        }
        first = false;
        json += "{";
        json += "\"index\":" + String(i);
        json += ",\"name\":\"" + String(g_channels[i].name) + "\"";
        json += ",\"type\":" + String(static_cast<int>(g_channels[i].type));
        json += ",\"type_label\":\"" + String(channelTypeToString(g_channels[i].type)) + "\"";
        json += ",\"pin\":" + String(g_channels[i].pin);
        json += ",\"input\":" + String(static_cast<int>(g_channels[i].input));
        json += ",\"input_secondary\":" + String(static_cast<int>(g_channels[i].inputSecondary));
        json += ",\"input_modifier\":" + String(static_cast<int>(g_channels[i].inputModifier));
        json += ",\"modifier_reverses\":";
        json += g_channels[i].modifierReverses ? "true" : "false";
        json += ",\"threshold\":" + String(g_channels[i].switchThresholdPercent);
        json += ",\"inverted\":";
        json += g_channels[i].inverted ? "true" : "false";
        float signedActivity = channelSignedActivity(g_channels[i], g_gamepad);
        float activity = fabsf(signedActivity);
        json += ",\"activity\":" + String(activity, 3);
        json += ",\"signed_activity\":" + String(signedActivity, 3);
        json += ",\"active\":";
        json += (activity > 0.15f) ? "true" : "false";
        json += "}";
    }
    json += "],\"pwm_pins\":[";
    bool firstPin = true;
    for (int pin = 0; pin <= 48; ++pin) {
        if (!ESP32PWM::hasPwm(pin)) {
            continue;
        }
        if (!firstPin) {
            json += ",";
        }
        firstPin = false;
        json += String(pin);
    }
    json += "],\"presets\":[\"";
    json += String(kPresetRcCar);
    json += "\",\"";
    json += String(kPresetExcavator);
    json += "\"";
    PresetDirectory dir = {};
    rcctl::loadPresetDirectory(&dir);
    for (int i = 0; i < static_cast<int>(dir.count); ++i) {
        json += ",\"" + String(dir.names[i]) + "\"";
    }
    json += "]}";
    g_server.send(200, "application/json", json);
}

void handleApiChannelAdd() {
    int index = firstFreeChannelIndex();
    if (index < 0) {
        sendJson(false, "Limite de channels atteinte");
        return;
    }
    ChannelConfig draft;
    draft.used = true;
    parseChannelFromRequest(&draft, index);
    g_channels[index] = draft;
    String error;
    if (!setupChannelHardware(index, &error)) {
        g_channels[index].used = false;
        sendJson(false, error);
        return;
    }
    g_modelDirty = true;
    sendJson(true, "Channel ajoute");
}

void handleApiChannelUpdate() {
    int index = parseIntArg("index", -1);
    if (index < 0 || index >= kMaxChannels || !g_channels[index].used) {
        sendJson(false, "Channel invalide");
        return;
    }
    ChannelConfig backup = g_channels[index];
    releaseChannelHardware(index);
    ChannelConfig draft = backup;
    parseChannelFromRequest(&draft, index);
    draft.used = true;
    g_channels[index] = draft;
    String error;
    if (!setupChannelHardware(index, &error)) {
        g_channels[index] = backup;
        String restoreError;
        setupChannelHardware(index, &restoreError);
        sendJson(false, error);
        return;
    }
    g_modelDirty = true;
    sendJson(true, "Channel mis a jour");
}

void handleApiChannelDelete() {
    int index = parseIntArg("index", -1);
    if (index < 0 || index >= kMaxChannels || !g_channels[index].used) {
        sendJson(false, "Channel invalide");
        return;
    }
    releaseChannelHardware(index);
    g_channels[index] = ChannelConfig();
    g_modelDirty = true;
    sendJson(true, "Channel supprime");
}

void handleApiChannelClear() {
    for (int i = 0; i < kMaxChannels; ++i) {
        if (g_channels[i].used) {
            releaseChannelHardware(i);
            g_channels[i] = ChannelConfig();
        }
    }
    g_modelDirty = true;
    sendJson(true, "Tous les channels supprimes");
}

void handleApiPresetSave() {
    String err;
    if (!saveUserPreset(g_server.arg("name"), &err)) {
        sendJson(false, err);
        return;
    }
    sendJson(true, "Preset sauvegarde");
}

void handleApiPresetApply() {
    PersistedConfig cfg = {};
    const String modelName = rcctl::sanitizePresetName(g_server.arg("name"));
    String err;
    if (!rcctl::loadAnyPreset(modelName, &cfg, &err)) {
        sendJson(false, err);
        return;
    }
    if (!applyPersistedConfig(cfg, &err)) {
        sendJson(false, err);
        return;
    }
    g_currentModelName = modelName;
    g_modelDirty = false;
    sendJson(true, "Preset applique");
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
    cfg.magic = kConfigMagic;
    cfg.version = kConfigVersion;
    const bool fromCurrent = parseBoolArg("from_current", false);
    String rawBase = g_server.hasArg("base") ? g_server.arg("base") : "";
    rawBase.trim();
    const bool cloneEnabled = !rawBase.isEmpty() && !rawBase.equalsIgnoreCase("none");
    String err;
    if (fromCurrent) {
        exportCurrentConfig(&cfg);
    } else if (cloneEnabled) {
        const String baseName = rcctl::sanitizePresetName(rawBase);
        if (!rcctl::loadAnyPreset(baseName, &cfg, &err)) {
            sendJson(false, err);
            return;
        }
    }
    if (!saveUserPreset(newName, &cfg, &err)) {
        sendJson(false, err);
        return;
    }
    if (!applyPersistedConfig(cfg, &err)) {
        sendJson(false, err);
        return;
    }
    g_currentModelName = newName;
    g_modelDirty = false;
    String msg = "Model created: ";
    msg += newName;
    sendJson(true, msg);
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
    if (!saveApSsid(nextSsid)) {
        sendJson(false, "Failed to save AP config");
        return;
    }
    if (!saveApPassword(nextPass)) {
        sendJson(false, "Failed to save AP password");
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

void handleApiModelSaveCurrent() {
    if (g_currentModelName == kPresetRcCar || g_currentModelName == kPresetExcavator) {
        const String suggested = nextAvailableCustomModelName((g_currentModelName == kPresetRcCar) ? String("car_custom")
                                                                                                    : String("excavator_custom"));
        String json = "{\"ok\":false,\"readonly\":true,\"message\":\"Readonly preset: create a fork to save changes\",\"suggested_name\":\"";
        json += suggested;
        json += "\",\"base\":\"";
        json += g_currentModelName;
        json += "\"}";
        g_server.send(200, "application/json", json);
        return;
    }
    String err;
    if (!saveUserPreset(g_currentModelName, &err)) {
        sendJson(false, err);
        return;
    }
    saveConfigToNvs();
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
    if (!applyPersistedConfig(cfg, &err)) {
        sendJson(false, err);
        return;
    }
    g_modelDirty = false;
    sendJson(true, "Changes reverted");
}

void handleApiModelDelete() {
    const String modelName = rcctl::sanitizePresetName(g_server.arg("name"));
    if (modelName == kPresetRcCar || modelName == kPresetExcavator) {
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
        applyPersistedConfig(cfg, &err);
        g_modelDirty = false;
    }
    sendJson(true, "Preset deleted");
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

void handleApiLearnStart() {
    int index = parseIntArg("index", -1);
    if (index < 0 || index >= kMaxChannels || !g_channels[index].used) {
        sendJson(false, "Channel invalide");
        return;
    }
    g_learningChannelIndex = index;
    g_learningCandidate = InputId::None;
    g_learningCandidateHits = 0;
    g_learningDeadlineMs = millis() + kLearnTimeoutMs;
    sendJson(true, "Apprentissage demarre");
}

void handleApiLearnCancel() {
    g_learningChannelIndex = -1;
    g_learningCandidate = InputId::None;
    g_learningCandidateHits = 0;
    g_learningDeadlineMs = 0;
    sendJson(true, "Apprentissage stop");
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

void handlePairingOn() {
    if (!kDiagDisableBluepad32) {
        g_pairingScanEnabled = true;
        g_pairingScanUntilMs = millis() + kPairingWindowMs;
        g_btScanToggleAtMs = 0;
        Console.println("BT pairing mode: ON (120s, scan pulse)");
    }
    redirectToRoot();
}

void handlePairingOff() {
    if (!kDiagDisableBluepad32) {
        g_pairingScanEnabled = false;
        g_pairingScanUntilMs = 0;
        g_btScanToggleAtMs = 0;
        Console.println("BT pairing mode: OFF");
    }
    redirectToRoot();
}

void setupWebUi() {
    WiFi.setSleep(false);
    WiFi.mode(WIFI_AP);
    const bool apOpen = g_apPassword.length() < 8;
    bool started = WiFi.softAP(g_apSsid.c_str(), apOpen ? nullptr : g_apPassword.c_str(), kApChannel, false, kApMaxConnections);
    if (!started) {
        Console.println("Erreur: impossible de demarrer le point d'acces Wi-Fi");
    } else {
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
            esp_err_t cfgErr = esp_wifi_set_config(WIFI_IF_AP, &apCfg);
            if (cfgErr != ESP_OK) {
                Console.printf("Wi-Fi AP: echec set_config (%d)\n", static_cast<int>(cfgErr));
            }
            esp_err_t bwErr = esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
            if (bwErr != ESP_OK) {
                Console.printf("Wi-Fi AP: echec set_bandwidth HT20 (%d)\n", static_cast<int>(bwErr));
            }
            esp_err_t protoErr = esp_wifi_set_protocol(WIFI_IF_AP, WIFI_PROTOCOL_11B | WIFI_PROTOCOL_11G);
            if (protoErr != ESP_OK) {
                Console.printf("Wi-Fi AP: echec set_protocol 11b/g (%d)\n", static_cast<int>(protoErr));
            }
            Console.printf("Wi-Fi AP cfg: channel=%u auth=%u max_conn=%u pmf_cap=%u pmf_req=%u\n",
                           apCfg.ap.channel,
                           static_cast<unsigned>(apCfg.ap.authmode),
                           apCfg.ap.max_connection,
                           static_cast<unsigned>(apCfg.ap.pmf_cfg.capable),
                           static_cast<unsigned>(apCfg.ap.pmf_cfg.required));
        } else {
            Console.println("Wi-Fi AP: echec get_config");
        }
    }

    WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
        if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
            Console.printf("AP: station connectee, aid=%u\n", info.wifi_ap_staconnected.aid);
        } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
            Console.printf("AP: station deconnectee, reason=%u aid=%u\n",
                           info.wifi_ap_stadisconnected.reason,
                           info.wifi_ap_stadisconnected.aid);
        } else if (event == ARDUINO_EVENT_WIFI_AP_STAIPASSIGNED) {
            const auto& ip = info.wifi_ap_staipassigned.ip;
            Console.printf("AP: IP assignee a une station: %u.%u.%u.%u\n",
                           ip.addr & 0xFF,
                           (ip.addr >> 8) & 0xFF,
                           (ip.addr >> 16) & 0xFF,
                           (ip.addr >> 24) & 0xFF);
        }
    });

    g_server.on("/", HTTP_GET, handleRoot);
    g_server.on("/generate_204", HTTP_GET, redirectCaptivePortal);      // Android
    g_server.on("/gen_204", HTTP_GET, redirectCaptivePortal);           // Android fallback
    g_server.on("/hotspot-detect.html", HTTP_GET, redirectCaptivePortal);  // Apple
    g_server.on("/connecttest.txt", HTTP_GET, redirectCaptivePortal);   // Windows
    g_server.on("/ncsi.txt", HTTP_GET, redirectCaptivePortal);          // Windows NCSI
    g_server.on("/fwlink", HTTP_GET, redirectCaptivePortal);            // Windows captive check
    g_server.on("/add", HTTP_POST, handleAddChannel);
    g_server.on("/update", HTTP_POST, handleUpdateChannel);
    g_server.on("/delete", HTTP_POST, handleDeleteChannel);
    g_server.on("/clear", HTTP_POST, handleClearChannels);
    g_server.on("/pairing_on", HTTP_POST, handlePairingOn);
    g_server.on("/pairing_off", HTTP_POST, handlePairingOff);
    g_server.on("/api/state", HTTP_GET, handleApiState);
    g_server.on("/api/channel_add", HTTP_POST, handleApiChannelAdd);
    g_server.on("/api/channel_update", HTTP_POST, handleApiChannelUpdate);
    g_server.on("/api/channel_delete", HTTP_POST, handleApiChannelDelete);
    g_server.on("/api/channel_clear", HTTP_POST, handleApiChannelClear);
    g_server.on("/api/preset_save", HTTP_POST, handleApiPresetSave);
    g_server.on("/api/preset_apply", HTTP_POST, handleApiPresetApply);
    g_server.on("/api/model_set_default", HTTP_POST, handleApiModelSetDefault);
    g_server.on("/api/model_create", HTTP_POST, handleApiModelCreate);
    g_server.on("/api/model_save_current", HTTP_POST, handleApiModelSaveCurrent);
    g_server.on("/api/model_revert", HTTP_POST, handleApiModelRevert);
    g_server.on("/api/model_delete", HTTP_POST, handleApiModelDelete);
    g_server.on("/api/ap_config_set", HTTP_POST, handleApiApConfigSet);
    g_server.on("/api/ap_config_apply_reboot", HTTP_POST, handleApiApConfigApplyReboot);
    g_server.on("/api/pairing_on", HTTP_POST, handleApiPairingOn);
    g_server.on("/api/pairing_off", HTTP_POST, handleApiPairingOff);
    g_server.on("/api/learn_start", HTTP_POST, handleApiLearnStart);
    g_server.on("/api/learn_cancel", HTTP_POST, handleApiLearnCancel);
    g_server.on("/api/learn_detect", HTTP_POST, handleApiLearnDetect);
    g_server.onNotFound([]() {
        if (g_server.method() == HTTP_GET) {
            redirectCaptivePortal();
            return;
        }
        g_server.send(404, "text/plain", "Not found");
    });

    g_server.begin();
    g_dnsServer.start(53, "*", WiFi.softAPIP());

    Console.printf("AP Wi-Fi pret: SSID=%s pass=%s IP=%s\n",
                   g_apSsid.c_str(),
                   g_apPassword.isEmpty() ? "<open>" : g_apPassword.c_str(),
                   WiFi.softAPIP().toString().c_str());
    Console.printf("Captive portal actif: DNS * -> %s\n", WiFi.softAPIP().toString().c_str());
}

void onConnectedController(ControllerPtr ctl) {
    if (!ctl) {
        Console.println("Controller invalide ignore");
        return;
    }

    if (ctl->isMouse() || ctl->isKeyboard() || ctl->isBalanceBoard()) {
        Console.println("Controller non compatible (mouse/keyboard/balance) ignore");
        return;
    }

    if (g_gamepad && g_gamepad->isConnected()) {
        Console.println("Un gamepad est deja actif, nouveau gamepad ignore");
        return;
    }

    g_gamepad = ctl;
    g_lastPacketMs = millis();
    g_signalTimedOut = false;
    if (g_pairingScanEnabled) {
        g_pairingScanEnabled = false;
        g_pairingScanUntilMs = 0;
        Console.println("BT pairing mode: OFF (manette connectee)");
    }
    g_btScanToggleAtMs = 0;
    setBtScanActive(false, "controller connected");

    Console.printf("Gamepad connecte: %s\n", ctl->getModelName());
}

void onDisconnectedController(ControllerPtr ctl) {
    if (ctl == g_gamepad) {
        g_gamepad = nullptr;
        g_signalTimedOut = false;
        applyFailsafeAllChannels();
        g_btScanToggleAtMs = 0;
        Console.println("Gamepad deconnecte -> failsafe global");
    }
}

}  // namespace

void setup() {
    Console.printf("Firmware: %s\n", BP32.firmwareVersion());
    const uint8_t* addr = BP32.localBdAddress();
    Console.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n", addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

    esp_err_t nvsErr = nvs_flash_init();
    if (nvsErr == ESP_ERR_NVS_NO_FREE_PAGES || nvsErr == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvsErr = nvs_flash_init();
    }
    if (nvsErr != ESP_OK) {
        Console.printf("NVS init KO: %d\n", static_cast<int>(nvsErr));
    }
    g_apSsid = loadApSsid();
    g_apPassword = loadApPassword();
    Console.printf("AP SSID config: %s\n", g_apSsid.c_str());

    esp_err_t coexErr = esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
    if (coexErr != ESP_OK) {
        Console.printf("COEX: preference Wi-Fi KO (%d)\n", static_cast<int>(coexErr));
    } else {
        Console.println("COEX: preference Wi-Fi active");
    }

    if (!kDiagDisableBluepad32) {
        // Keep BT stack active but disable continuous scan by default to reduce RF conflicts with Wi-Fi AP.
        BP32.setup(&onConnectedController, &onDisconnectedController, false);
        BP32.enableVirtualDevice(false);
        BP32.enableBLEService(false);
        setBtScanActive(false, "startup");
        g_pairingScanEnabled = false;
        g_pairingScanUntilMs = 0;
        g_btScanToggleAtMs = millis() + 5000;
        Console.println("BT scan pulse scheduler actif (auto-reconnect)");
    } else {
        Console.println("DIAG: Bluepad32 desactive temporairement pour test AP Wi-Fi");
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
    if (applyPersistedConfig(cfg, &err)) {
        Console.printf("Boot preset loaded: %s\n", g_bootModelName.c_str());
    } else {
        Console.printf("Boot preset load failed: %s\n", err.c_str());
    }
    g_modelDirty = false;
    setupWebUi();
    applyFailsafeAllChannels();
}

void loop() {
    if (!kDiagDisableBluepad32) {
        BP32.update();
        updateBtScanScheduler();
    }

    if (g_learningChannelIndex >= 0) {
        const bool timedOut = millis() > g_learningDeadlineMs;
        const bool hasPad = g_gamepad && g_gamepad->isConnected();
        if (timedOut || !hasPad) {
            g_learningChannelIndex = -1;
            g_learningCandidate = InputId::None;
            g_learningCandidateHits = 0;
        } else {
            const InputId hit = detectDominantInput(g_gamepad);
            if (hit != InputId::None) {
                if (hit == g_learningCandidate) {
                    g_learningCandidateHits++;
                } else {
                    g_learningCandidate = hit;
                    g_learningCandidateHits = 1;
                }
                if (g_learningCandidateHits >= 2) {
                    g_channels[g_learningChannelIndex].input = hit;
                    g_modelDirty = true;
                    const InputDefinition* d = getInputDefinition(hit);
                    Console.printf("Learning: CH%d -> %s\n",
                                   g_learningChannelIndex,
                                   d ? d->label : "unknown");
                    g_learningChannelIndex = -1;
                    g_learningCandidate = InputId::None;
                    g_learningCandidateHits = 0;
                }
            }
        }
    }

    g_dnsServer.processNextRequest();
    g_server.handleClient();

    if (g_gamepad && g_gamepad->isConnected()) {
        if (g_gamepad->hasData()) {
            g_lastPacketMs = millis();
            g_signalTimedOut = false;
            processGamepadToChannels(g_gamepad);
        } else {
            const uint32_t now = millis();
            if (now - g_lastPacketMs > kSignalTimeoutMs) {
                applyFailsafeAllChannels();
                if (!g_signalTimedOut) {
                    g_signalTimedOut = true;
                    Console.printf("Failsafe timeout: aucun paquet depuis %lu ms\n", now - g_lastPacketMs);
                }
            }
        }
    } else {
        applyFailsafeAllChannels();
    }

    const uint32_t now = millis();
    if (now - g_lastUiLogMs > kUiRefreshMs) {
        g_lastUiLogMs = now;
        int active = 0;
        for (const auto& ch : g_channels) {
            if (ch.used) {
                active++;
            }
        }
        Console.printf("Status: channels=%d gamepad=%s\n", active, (g_gamepad && g_gamepad->isConnected()) ? "ON" : "OFF");
    }

    delay(20);
}
