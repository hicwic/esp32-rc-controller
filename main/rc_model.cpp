#include "rc_model.h"

#include <math.h>
#include <nvs.h>

namespace rcctl {

namespace {

constexpr int kPwmMinUs = 1000;
constexpr int kPwmNeutralUs = 1500;
constexpr int kPwmMaxUs = 2000;

VirtualInputConfig g_virtualInputs[kMaxVirtualInputs];
OutputChannelConfig g_outputs[kMaxOutputChannels];
float g_virtualRuntime[kMaxVirtualInputs] = {0.0f};
float g_outputRuntime[kMaxOutputChannels] = {0.0f};
Servo g_pwmOutputs[kMaxOutputChannels];
bool g_pwmAttached[kMaxOutputChannels] = {false};

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

float applyExpoSigned(float v, int expoPercent) {
    const float e = constrain(expoPercent, 0, 100) / 100.0f;
    if (e <= 0.0f) {
        return constrain(v, -1.0f, 1.0f);
    }
    const float power = 1.0f + e * 2.0f;
    const float a = powf(fabsf(v), power);
    const float out = (v < 0.0f) ? -a : a;
    return constrain(out, -1.0f, 1.0f);
}

float sourceValueByIndex(int8_t idx) {
    if (idx < 0 || idx >= kMaxVirtualInputs) {
        return 0.0f;
    }
    return g_virtualRuntime[idx];
}

int pwmFromOutput(const OutputChannelConfig& out) {
    const float n = evaluateOutputSignal(out);
    const int us = static_cast<int>(lroundf(kPwmNeutralUs + n * static_cast<float>(kPwmMaxUs - kPwmNeutralUs)));
    return applyPwmInvert(clampPwmUs(us), false);
}

int switchFromOutput(const OutputChannelConfig& out) {
    const float n = evaluateOutputSignal(out);
    const float analog01 = (n + 1.0f) * 0.5f;
    const float threshold = constrain(out.thresholdPercent, 0, 100) / 100.0f;
    return analog01 >= threshold ? HIGH : LOW;
}

}  // namespace

VirtualInputConfig* virtualInputs() {
    return g_virtualInputs;
}

OutputChannelConfig* outputs() {
    return g_outputs;
}

float* virtualRuntimeValues() {
    return g_virtualRuntime;
}

float* outputRuntimeValues() {
    return g_outputRuntime;
}

bool* pwmAttached() {
    return g_pwmAttached;
}

int firstFreeVirtualIndex() {
    for (int i = 0; i < kMaxVirtualInputs; ++i) {
        if (!g_virtualInputs[i].used) {
            return i;
        }
    }
    return -1;
}

int firstFreeOutputIndex() {
    for (int i = 0; i < kMaxOutputChannels; ++i) {
        if (!g_outputs[i].used) {
            return i;
        }
    }
    return -1;
}

bool outputPinAlreadyUsed(uint8_t pin, int ignoreIndex) {
    for (int i = 0; i < kMaxOutputChannels; ++i) {
        if (i == ignoreIndex) {
            continue;
        }
        if (g_outputs[i].used && g_outputs[i].pin == pin) {
            return true;
        }
    }
    return false;
}

float evaluateVirtualInput(const VirtualInputConfig& in, ControllerPtr ctl) {
    if (!in.used || in.primary == InputId::None || !ctl || !ctl->isConnected()) {
        return 0.0f;
    }

    float v = normalizedForInput(in.primary, ctl);
    if (in.secondary != InputId::None) {
        v -= normalizedForInput(in.secondary, ctl);
    }
    v = constrain(v, -1.0f, 1.0f);

    if (in.modifierFunction == ModifierFunction::Reverse && in.modifier != InputId::None && isInputActive(in.modifier, ctl)) {
        v = -v;
    }

    v = applyDeadzoneSigned(v, in.deadzonePercent);
    v = applyExpoSigned(v, in.expoPercent);
    return constrain(v, -1.0f, 1.0f);
}

float evaluateOutputSignal(const OutputChannelConfig& out) {
    if (!out.used) {
        return 0.0f;
    }
    float v = 0.0f;
    v += sourceValueByIndex(out.sourceA) * (static_cast<float>(out.weightA) / 100.0f);
    v += sourceValueByIndex(out.sourceB) * (static_cast<float>(out.weightB) / 100.0f);
    v += sourceValueByIndex(out.sourceC) * (static_cast<float>(out.weightC) / 100.0f);
    v = constrain(v, -1.0f, 1.0f);
    if (out.inverted) {
        v = -v;
    }
    return v;
}

void evaluateVirtualRuntime(ControllerPtr ctl) {
    for (int i = 0; i < kMaxVirtualInputs; ++i) {
        g_virtualRuntime[i] = evaluateVirtualInput(g_virtualInputs[i], ctl);
    }
}

void processGamepadToOutputs(ControllerPtr ctl) {
    evaluateVirtualRuntime(ctl);
    for (int i = 0; i < kMaxOutputChannels; ++i) {
        if (!g_outputs[i].used) {
            g_outputRuntime[i] = 0.0f;
            continue;
        }

        const float signal = evaluateOutputSignal(g_outputs[i]);
        g_outputRuntime[i] = signal;

        if (g_outputs[i].type == ChannelType::Pwm) {
            if (g_pwmAttached[i]) {
                g_pwmOutputs[i].writeMicroseconds(pwmFromOutput(g_outputs[i]));
            }
        } else {
            digitalWrite(g_outputs[i].pin, switchFromOutput(g_outputs[i]));
        }
    }
}

void releaseOutputHardware(int index) {
    if (g_pwmAttached[index]) {
        g_pwmOutputs[index].detach();
        g_pwmAttached[index] = false;
    }
}

bool setupOutputHardware(int index, String* error) {
    const OutputChannelConfig& out = g_outputs[index];
    if (outputPinAlreadyUsed(out.pin, index)) {
        if (error) {
            *error = "Pin already used";
        }
        return false;
    }

    if (out.type == ChannelType::Pwm) {
        g_pwmOutputs[index].setPeriodHertz(50);
        int attached = g_pwmOutputs[index].attach(out.pin, kPwmMinUs, kPwmMaxUs);
        if (attached <= 0) {
            if (error) {
                *error = "PWM attach failed";
            }
            return false;
        }
        g_pwmAttached[index] = true;
    } else {
        pinMode(out.pin, OUTPUT);
        digitalWrite(out.pin, LOW);
    }

    writeFailsafeForOutput(index);
    return true;
}

void writeFailsafeForOutput(int index) {
    if (!g_outputs[index].used) {
        return;
    }
    if (g_outputs[index].type == ChannelType::Pwm) {
        if (g_pwmAttached[index]) {
            g_pwmOutputs[index].writeMicroseconds(kPwmNeutralUs);
        }
    } else {
        pinMode(g_outputs[index].pin, OUTPUT);
        digitalWrite(g_outputs[index].pin, LOW);
    }
    g_outputRuntime[index] = 0.0f;
}

void applyFailsafeAllOutputs() {
    for (int i = 0; i < kMaxOutputChannels; ++i) {
        writeFailsafeForOutput(i);
    }
}

bool exportCurrentConfig(PersistedConfig* out) {
    if (!out) {
        return false;
    }
    PersistedConfig cfg = {};
    cfg.magic = kConfigMagic;
    cfg.version = kConfigVersion;

    for (int i = 0; i < kMaxVirtualInputs; ++i) {
        cfg.virtual_inputs[i].used = g_virtualInputs[i].used ? 1 : 0;
        cfg.virtual_inputs[i].input = static_cast<uint8_t>(g_virtualInputs[i].primary);
        cfg.virtual_inputs[i].input_secondary = static_cast<uint8_t>(g_virtualInputs[i].secondary);
        cfg.virtual_inputs[i].input_modifier = static_cast<uint8_t>(g_virtualInputs[i].modifier);
        cfg.virtual_inputs[i].modifier_function = static_cast<uint8_t>(g_virtualInputs[i].modifierFunction);
        cfg.virtual_inputs[i].deadzone = static_cast<uint8_t>(constrain(g_virtualInputs[i].deadzonePercent, 0, 95));
        cfg.virtual_inputs[i].expo = static_cast<uint8_t>(constrain(g_virtualInputs[i].expoPercent, 0, 100));
        memcpy(cfg.virtual_inputs[i].name, g_virtualInputs[i].name, sizeof(cfg.virtual_inputs[i].name));
    }

    for (int i = 0; i < kMaxOutputChannels; ++i) {
        cfg.channels[i].used = g_outputs[i].used ? 1 : 0;
        cfg.channels[i].type = static_cast<uint8_t>(g_outputs[i].type);
        cfg.channels[i].pin = g_outputs[i].pin;
        cfg.channels[i].inverted = g_outputs[i].inverted ? 1 : 0;
        cfg.channels[i].source_a = g_outputs[i].sourceA;
        cfg.channels[i].source_b = g_outputs[i].sourceB;
        cfg.channels[i].source_c = g_outputs[i].sourceC;
        cfg.channels[i].weight_a = g_outputs[i].weightA;
        cfg.channels[i].weight_b = g_outputs[i].weightB;
        cfg.channels[i].weight_c = g_outputs[i].weightC;
        cfg.channels[i].threshold = static_cast<uint8_t>(constrain(g_outputs[i].thresholdPercent, 0, 100));
        memcpy(cfg.channels[i].name, g_outputs[i].name, sizeof(cfg.channels[i].name));
    }

    *out = cfg;
    return true;
}

bool applyPersistedConfig(const PersistedConfig& cfg, String* errorOut) {
    if (cfg.magic != kConfigMagic || cfg.version != kConfigVersion) {
        if (errorOut) {
            *errorOut = "Invalid preset format";
        }
        return false;
    }

    VirtualInputConfig oldInputs[kMaxVirtualInputs];
    OutputChannelConfig oldOutputs[kMaxOutputChannels];
    bool oldPwm[kMaxOutputChannels];
    for (int i = 0; i < kMaxVirtualInputs; ++i) {
        oldInputs[i] = g_virtualInputs[i];
        g_virtualInputs[i] = VirtualInputConfig();
    }
    for (int i = 0; i < kMaxOutputChannels; ++i) {
        oldOutputs[i] = g_outputs[i];
        oldPwm[i] = g_pwmAttached[i];
        if (g_outputs[i].used) {
            releaseOutputHardware(i);
        }
        g_outputs[i] = OutputChannelConfig();
    }

    for (int i = 0; i < kMaxVirtualInputs; ++i) {
        const PersistedVirtualInput& pv = cfg.virtual_inputs[i];
        if (!pv.used) {
            continue;
        }
        g_virtualInputs[i].used = true;
        g_virtualInputs[i].primary = static_cast<InputId>(pv.input);
        g_virtualInputs[i].secondary = static_cast<InputId>(pv.input_secondary);
        g_virtualInputs[i].modifier = static_cast<InputId>(pv.input_modifier);
        g_virtualInputs[i].modifierFunction = (pv.modifier_function == static_cast<uint8_t>(ModifierFunction::Reverse))
                                                  ? ModifierFunction::Reverse
                                                  : ModifierFunction::None;
        g_virtualInputs[i].deadzonePercent = constrain(pv.deadzone, 0, 95);
        g_virtualInputs[i].expoPercent = constrain(pv.expo, 0, 100);
        memcpy(g_virtualInputs[i].name, pv.name, sizeof(g_virtualInputs[i].name));
        g_virtualInputs[i].name[sizeof(g_virtualInputs[i].name) - 1] = '\0';
    }

    auto restoreOld = [&]() {
        for (int i = 0; i < kMaxVirtualInputs; ++i) {
            g_virtualInputs[i] = oldInputs[i];
        }
        for (int i = 0; i < kMaxOutputChannels; ++i) {
            if (g_outputs[i].used) {
                releaseOutputHardware(i);
            }
            g_outputs[i] = oldOutputs[i];
            g_pwmAttached[i] = false;
            if (g_outputs[i].used && oldPwm[i]) {
                String restoreError;
                setupOutputHardware(i, &restoreError);
            }
        }
    };

    for (int i = 0; i < kMaxOutputChannels; ++i) {
        const auto& pc = cfg.channels[i];
        if (!pc.used) {
            continue;
        }
        auto sourceValid = [](int8_t s) {
            return s >= -1 && s < kMaxVirtualInputs;
        };
        if (!sourceValid(pc.source_a) || !sourceValid(pc.source_b) || !sourceValid(pc.source_c)) {
            restoreOld();
            if (errorOut) {
                *errorOut = "Invalid output source index";
            }
            return false;
        }

        g_outputs[i].used = true;
        g_outputs[i].type = (pc.type == static_cast<uint8_t>(ChannelType::Switch)) ? ChannelType::Switch : ChannelType::Pwm;
        g_outputs[i].pin = pc.pin;
        g_outputs[i].inverted = pc.inverted != 0;
        g_outputs[i].sourceA = pc.source_a;
        g_outputs[i].sourceB = pc.source_b;
        g_outputs[i].sourceC = pc.source_c;
        g_outputs[i].weightA = constrain(pc.weight_a, -100, 100);
        g_outputs[i].weightB = constrain(pc.weight_b, -100, 100);
        g_outputs[i].weightC = constrain(pc.weight_c, -100, 100);
        g_outputs[i].thresholdPercent = constrain(pc.threshold, 0, 100);
        memcpy(g_outputs[i].name, pc.name, sizeof(g_outputs[i].name));
        g_outputs[i].name[sizeof(g_outputs[i].name) - 1] = '\0';

        String error;
        if (!setupOutputHardware(i, &error)) {
            restoreOld();
            if (errorOut) {
                *errorOut = "Invalid preset: " + error;
            }
            return false;
        }
    }

    applyFailsafeAllOutputs();
    saveRuntimeConfigToNvs();
    return true;
}

bool saveRuntimeConfigToNvs() {
    PersistedConfig cfg = {};
    if (!exportCurrentConfig(&cfg)) {
        return false;
    }
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }
    err = nvs_set_blob(handle, "cfg", &cfg, sizeof(cfg));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

String outputTypeLabel(ChannelType type) {
    return type == ChannelType::Pwm ? "PWM" : "ON/OFF";
}

}  // namespace rcctl

