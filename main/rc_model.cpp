#include "rc_model.h"

#include <math.h>
#include <nvs.h>

namespace rcctl {

namespace {
#ifdef DEBUG
#define RCCTL_DEBUG_LOG 1
#else
#define RCCTL_DEBUG_LOG 0
#endif

constexpr int kPwmMinUs = 1000;
constexpr int kPwmNeutralUs = 1500;
constexpr int kPwmMaxUs = 2000;

VirtualInputConfig g_virtualInputs[kMaxVirtualInputs];
OutputChannelConfig g_outputs[kMaxOutputChannels];
float g_virtualRuntime[kMaxVirtualInputs] = {0.0f};
float g_outputRuntime[kMaxOutputChannels] = {0.0f};
Servo g_pwmOutputs[kMaxOutputChannels];
bool g_pwmAttached[kMaxOutputChannels] = {false};
int8_t g_togglePosition[kMaxVirtualInputs] = {0};
bool g_togglePrimaryPressedPrev[kMaxVirtualInputs] = {false};
bool g_toggleSecondaryPressedPrev[kMaxVirtualInputs] = {false};
bool g_toggleModifierPressedPrev[kMaxVirtualInputs] = {false};
int8_t g_toggle3Direction[kMaxVirtualInputs] = {1};
bool g_directPrimaryPressedPrev[kMaxVirtualInputs] = {false};
uint32_t g_lastRumbleMs[kMaxVirtualInputs] = {0};

constexpr float kRumbleGain = 2.6f;

uint8_t scaleRumble(uint8_t in) {
    return static_cast<uint8_t>(constrain(lroundf(static_cast<float>(in) * kRumbleGain), 0, 255));
}

void playInputRumble(ControllerPtr ctl, int index, uint8_t weak, uint8_t strong, uint16_t durationMs) {
    if (!ctl || !ctl->isConnected() || index < 0 || index >= kMaxVirtualInputs) {
        return;
    }
    const uint32_t now = millis();
    if (now - g_lastRumbleMs[index] < 80) {
        return;
    }
    g_lastRumbleMs[index] = now;
    ctl->playDualRumble(0, durationMs, scaleRumble(weak), scaleRumble(strong));
}

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

bool isInputEnabledByModifier(const VirtualInputConfig& in, bool modifierPressed) {
    if (in.modifier == InputId::None) {
        return true;
    }
    if (in.modifierFunction == ModifierFunction::Activate) {
        return modifierPressed;
    }
    if (in.modifierFunction == ModifierFunction::Desactivate) {
        return !modifierPressed;
    }
    return true;
}

float evaluateToggleVirtualInput(int index, const VirtualInputConfig& in, ControllerPtr ctl, int positions) {
    if (index < 0 || index >= kMaxVirtualInputs || !ctl || !ctl->isConnected()) {
        return 0.0f;
    }
    const bool primaryPressed = isInputActive(in.primary, ctl);
    const bool primaryRise = primaryPressed && !g_togglePrimaryPressedPrev[index];
    g_togglePrimaryPressedPrev[index] = primaryPressed;

    const bool secondaryPressed = (in.secondary != InputId::None) ? isInputActive(in.secondary, ctl) : false;
    const bool secondaryRise = secondaryPressed && !g_toggleSecondaryPressedPrev[index];
    g_toggleSecondaryPressedPrev[index] = secondaryPressed;

    const bool modifierPressed = (in.modifier != InputId::None) ? isInputActive(in.modifier, ctl) : false;
    const bool modifierRise = modifierPressed && !g_toggleModifierPressedPrev[index];
    g_toggleModifierPressedPrev[index] = modifierPressed;

    if (!isInputEnabledByModifier(in, modifierPressed)) {
        return 0.0f;
    }

    if (modifierRise && positions == 3 && in.modifierFunction == ModifierFunction::Center) {
        g_togglePosition[index] = 1;  // center
        g_toggle3Direction[index] = 1;
    } else if (positions == 2 && primaryRise) {
        g_togglePosition[index] = (g_togglePosition[index] == 0) ? 1 : 0;
    } else if (positions == 3 && in.secondary != InputId::None) {
        // Direct 3-position control with dedicated buttons.
        if (primaryRise && !secondaryRise) {
            g_togglePosition[index] = 2;  // +1
        } else if (secondaryRise && !primaryRise) {
            g_togglePosition[index] = 0;  // -1
        }
    } else if (primaryRise) {
        // Fallback cycle mode for single-button usage.
        if (positions == 2) {
            g_togglePosition[index] = (g_togglePosition[index] == 0) ? 1 : 0;
        } else {  // 3-position cycle: -1 -> 0 -> +1 -> 0 -> -1 ...
            if (g_togglePosition[index] <= 0) {
                g_toggle3Direction[index] = 1;
            } else if (g_togglePosition[index] >= 2) {
                g_toggle3Direction[index] = -1;
            }
            g_togglePosition[index] = static_cast<int8_t>(constrain(g_togglePosition[index] + g_toggle3Direction[index], 0, 2));
        }
    }

    const bool bipolar = (in.toggleRange == ToggleRangeMode::Bipolar);
    if (positions == 2) {
        if (bipolar) {
            return g_togglePosition[index] ? 1.0f : -1.0f;  // -100 / +100
        }
        return g_togglePosition[index] ? 1.0f : 0.0f;  // 0 / 100
    }
    if (bipolar) {
        return static_cast<float>(g_togglePosition[index] - 1);  // -100 / 0 / +100
    }
    return static_cast<float>(g_togglePosition[index]) * 0.5f;  // 0 / 50 / 100
}

int pwmFromOutput(const OutputChannelConfig& out) {
    const float n = evaluateOutputSignal(out);
    const int us = static_cast<int>(lroundf(kPwmNeutralUs + n * static_cast<float>(kPwmMaxUs - kPwmNeutralUs)));
    return applyPwmInvert(clampPwmUs(us), false);
}

int switchFromOutput(const OutputChannelConfig& out) {
    const float n = evaluateOutputSignal(out);
    const float analog01 = constrain(fabsf(n), 0.0f, 1.0f);
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

int firstAvailablePwmPin(int ignoreIndex) {
    for (int pin = 0; pin <= 48; ++pin) {
        if (!ESP32PWM::hasPwm(pin)) {
            continue;
        }
        if (outputPinAlreadyUsed(static_cast<uint8_t>(pin), ignoreIndex)) {
            continue;
        }
        return pin;
    }
    return -1;
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

    if (!isInputEnabledByModifier(in, (in.modifier != InputId::None) ? isInputActive(in.modifier, ctl) : false)) {
        return 0.0f;
    }

    v = applyDeadzoneSigned(v, in.deadzonePercent);
    v = applyExpoSigned(v, in.expoPercent);
    return constrain(v, -1.0f, 1.0f);
}

float evaluateOutputSignal(const OutputChannelConfig& out) {
    if (!out.used) {
        return 0.0f;
    }
    const float tA = sourceValueByIndex(out.sourceA) * (static_cast<float>(out.weightA) / 100.0f) +
                     (static_cast<float>(out.offsetA) / 100.0f);
    const float tB = sourceValueByIndex(out.sourceB) * (static_cast<float>(out.weightB) / 100.0f) +
                     (static_cast<float>(out.offsetB) / 100.0f);
    const float tC = sourceValueByIndex(out.sourceC) * (static_cast<float>(out.weightC) / 100.0f) +
                     (static_cast<float>(out.offsetC) / 100.0f);

    float v = 0.0f;
    if (out.mixMode == MixMode::Multiply) {
        bool hasTerm = false;
        float p = 1.0f;
        if (out.sourceA >= 0 || out.offsetA != 0) {
            p *= tA;
            hasTerm = true;
        }
        if (out.sourceB >= 0 || out.offsetB != 0) {
            p *= tB;
            hasTerm = true;
        }
        if (out.sourceC >= 0 || out.offsetC != 0) {
            p *= tC;
            hasTerm = true;
        }
        v = hasTerm ? p : 0.0f;
    } else {
        v = tA + tB + tC;
    }
    v = constrain(v, -1.0f, 1.0f);
    if (out.inverted) {
        v = -v;
    }
    return v;
}

void evaluateVirtualRuntime(ControllerPtr ctl) {
    for (int i = 0; i < kMaxVirtualInputs; ++i) {
        const auto type = g_virtualInputs[i].inputType;
        if (!ctl || !ctl->isConnected()) {
            g_togglePrimaryPressedPrev[i] = false;
            g_toggleSecondaryPressedPrev[i] = false;
            g_toggleModifierPressedPrev[i] = false;
            g_directPrimaryPressedPrev[i] = false;
            g_virtualRuntime[i] = 0.0f;
            continue;
        }
        if (type == InputType::Toggle2Pos) {
            const int8_t prevPos = g_togglePosition[i];
            g_virtualRuntime[i] = evaluateToggleVirtualInput(i, g_virtualInputs[i], ctl, 2);
            if (g_virtualInputs[i].rumbleEnabled && prevPos != g_togglePosition[i]) {
                if (g_togglePosition[i] == 0) {
                    playInputRumble(ctl, i, 0x80, 0xC0, 90);
                } else {
                    playInputRumble(ctl, i, 0xC0, 0xFF, 140);
                }
            }
            continue;
        }
        if (type == InputType::Toggle3Pos) {
            const int8_t prevPos = g_togglePosition[i];
            g_virtualRuntime[i] = evaluateToggleVirtualInput(i, g_virtualInputs[i], ctl, 3);
            if (g_virtualInputs[i].rumbleEnabled && prevPos != g_togglePosition[i]) {
                switch (g_togglePosition[i]) {
                    case 0:
                        playInputRumble(ctl, i, 0x70, 0xB0, 80);
                        break;
                    case 1:
                        playInputRumble(ctl, i, 0xA0, 0xE0, 115);
                        break;
                    default:
                        playInputRumble(ctl, i, 0xD0, 0xFF, 160);
                        break;
                }
            }
            continue;
        }
        const bool primaryPressed = isInputActive(g_virtualInputs[i].primary, ctl);
        const bool primaryRise = primaryPressed && !g_directPrimaryPressedPrev[i];
        g_directPrimaryPressedPrev[i] = primaryPressed;
        g_togglePrimaryPressedPrev[i] = false;
        g_toggleSecondaryPressedPrev[i] = false;
        g_toggleModifierPressedPrev[i] = false;
        g_toggle3Direction[i] = 1;
        g_virtualRuntime[i] = evaluateVirtualInput(g_virtualInputs[i], ctl);
        const InputDefinition* def = getInputDefinition(g_virtualInputs[i].primary);
        const bool digitalPrimary = def && def->kind == InputKind::Digital;
        if (g_virtualInputs[i].rumbleEnabled && digitalPrimary && primaryRise) {
            playInputRumble(ctl, i, 0xB0, 0xFF, 120);
        }
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
    OutputChannelConfig& out = g_outputs[index];
    if (out.type == ChannelType::Pwm) {
        g_pwmOutputs[index].setPeriodHertz(50);
        const uint8_t requestedPin = out.pin;
        // Try requested pin first, then scan alternatives compatible with the current target.
        // This allows presets to travel across boards with different PWM-capable pin maps.
        auto tryAttachOnPin = [&](uint8_t pin) {
            if (!ESP32PWM::hasPwm(pin)) {
                return false;
            }
            if (outputPinAlreadyUsed(pin, index)) {
                return false;
            }
            // Clear any previous attempt before trying a new pin/channel combo.
            if (g_pwmOutputs[index].attached()) {
                g_pwmOutputs[index].detach();
            }
            g_pwmOutputs[index].attach(pin, kPwmMinUs, kPwmMaxUs);
            if (!g_pwmOutputs[index].attached()) {
                return false;
            }
            out.pin = pin;
            g_pwmAttached[index] = true;
            return true;
        };

        if (!tryAttachOnPin(requestedPin)) {
            for (int pin = 0; pin <= 48; ++pin) {
                if (pin == requestedPin) {
                    continue;
                }
                if (tryAttachOnPin(static_cast<uint8_t>(pin))) {
#if RCCTL_DEBUG_LOG
                    Console.printf("PWM pin remap: CH%d GPIO %d -> GPIO %d\n", index, requestedPin, pin);
#endif
                    break;
                }
            }
            if (!g_pwmAttached[index]) {
                if (error) {
                    *error = "PWM attach failed on GPIO " + String(requestedPin);
                }
                return false;
            }
        }
    } else {
        if (outputPinAlreadyUsed(out.pin, index)) {
            if (error) {
                *error = "Pin already used";
            }
            return false;
        }
        pinMode(out.pin, OUTPUT);
        digitalWrite(out.pin, LOW);
    }

    writeFailsafeForOutput(index);
    return true;
}

bool rebuildOutputHardware(String* error) {
    for (int i = 0; i < kMaxOutputChannels; ++i) {
        if (g_outputs[i].used) {
            releaseOutputHardware(i);
        }
    }

    for (int i = 0; i < kMaxOutputChannels; ++i) {
        if (!g_outputs[i].used) {
            continue;
        }
        String localError;
        if (!setupOutputHardware(i, &localError)) {
            if (error) {
                *error = localError;
            }
            return false;
        }
    }
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
        cfg.virtual_inputs[i].reserved = static_cast<uint8_t>(static_cast<uint8_t>(g_virtualInputs[i].inputType) |
                                                              (static_cast<uint8_t>(g_virtualInputs[i].toggleRange) << 4) |
                                                              ((g_virtualInputs[i].rumbleEnabled ? 1 : 0) << 5));
        cfg.virtual_inputs[i].deadzone = static_cast<uint8_t>(constrain(g_virtualInputs[i].deadzonePercent, 0, 95));
        cfg.virtual_inputs[i].expo = static_cast<uint8_t>(constrain(g_virtualInputs[i].expoPercent, 0, 100));
        memcpy(cfg.virtual_inputs[i].name, g_virtualInputs[i].name, sizeof(cfg.virtual_inputs[i].name));
    }

    for (int i = 0; i < kMaxOutputChannels; ++i) {
        cfg.channels[i].used = g_outputs[i].used ? 1 : 0;
        cfg.channels[i].type = static_cast<uint8_t>(g_outputs[i].type);
        cfg.channels[i].pin = g_outputs[i].pin;
        cfg.channels[i].inverted = g_outputs[i].inverted ? 1 : 0;
        cfg.channels[i].mix_mode = static_cast<uint8_t>(g_outputs[i].mixMode);
        cfg.channels[i].source_a = g_outputs[i].sourceA;
        cfg.channels[i].source_b = g_outputs[i].sourceB;
        cfg.channels[i].source_c = g_outputs[i].sourceC;
        cfg.channels[i].weight_a = g_outputs[i].weightA;
        cfg.channels[i].weight_b = g_outputs[i].weightB;
        cfg.channels[i].weight_c = g_outputs[i].weightC;
        cfg.channels[i].offset_a = g_outputs[i].offsetA;
        cfg.channels[i].offset_b = g_outputs[i].offsetB;
        cfg.channels[i].offset_c = g_outputs[i].offsetC;
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
        const uint8_t inputTypeRaw = (pv.reserved & 0x0F);
        const uint8_t rangeRaw = (pv.reserved >> 4) & 0x01;
        const uint8_t rumbleRaw = (pv.reserved >> 5) & 0x01;
        switch (inputTypeRaw) {
            case static_cast<uint8_t>(InputType::Toggle2Pos):
                g_virtualInputs[i].inputType = InputType::Toggle2Pos;
                break;
            case static_cast<uint8_t>(InputType::Toggle3Pos):
                g_virtualInputs[i].inputType = InputType::Toggle3Pos;
                break;
            default:
                g_virtualInputs[i].inputType = InputType::Direct;
                break;
        }
        g_virtualInputs[i].toggleRange = (rangeRaw == 1) ? ToggleRangeMode::Bipolar : ToggleRangeMode::Unipolar;
        g_virtualInputs[i].rumbleEnabled = rumbleRaw == 1;
        switch (pv.modifier_function) {
            case static_cast<uint8_t>(ModifierFunction::Reverse):
                g_virtualInputs[i].modifierFunction = ModifierFunction::Reverse;
                break;
            case static_cast<uint8_t>(ModifierFunction::Center):
                g_virtualInputs[i].modifierFunction = ModifierFunction::Center;
                break;
            case static_cast<uint8_t>(ModifierFunction::Activate):
                g_virtualInputs[i].modifierFunction = ModifierFunction::Activate;
                break;
            case static_cast<uint8_t>(ModifierFunction::Desactivate):
                g_virtualInputs[i].modifierFunction = ModifierFunction::Desactivate;
                break;
            default:
                g_virtualInputs[i].modifierFunction = ModifierFunction::None;
                break;
        }
        g_togglePrimaryPressedPrev[i] = false;
        g_toggleSecondaryPressedPrev[i] = false;
        g_toggleModifierPressedPrev[i] = false;
        g_directPrimaryPressedPrev[i] = false;
        g_lastRumbleMs[i] = 0;
        if (g_virtualInputs[i].inputType == InputType::Toggle2Pos) {
            g_togglePosition[i] = 0;
        } else if (g_virtualInputs[i].inputType == InputType::Toggle3Pos) {
            // 3-position startup:
            // - Unipolar: start at 0% so first press goes to 50%, then 100%.
            // - Bipolar: start at center (0) to keep symmetric behavior.
            g_togglePosition[i] =
                (g_virtualInputs[i].toggleRange == ToggleRangeMode::Unipolar) ? 0 : 1;
            g_toggle3Direction[i] = 1;
        } else {
            g_togglePosition[i] = 0;
            g_toggle3Direction[i] = 1;
        }
        g_virtualInputs[i].deadzonePercent = constrain(pv.deadzone, 0, 95);
        g_virtualInputs[i].expoPercent = constrain(pv.expo, 0, 100);
        memcpy(g_virtualInputs[i].name, pv.name, sizeof(g_virtualInputs[i].name));
        g_virtualInputs[i].name[sizeof(g_virtualInputs[i].name) - 1] = '\0';
    }

    auto restoreOld = [&]() {
        // Transaction rollback: if any output setup fails, bring previous runtime config back.
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
        g_outputs[i].mixMode = (pc.mix_mode == static_cast<uint8_t>(MixMode::Multiply)) ? MixMode::Multiply : MixMode::Add;
        g_outputs[i].sourceA = pc.source_a;
        g_outputs[i].sourceB = pc.source_b;
        g_outputs[i].sourceC = pc.source_c;
        g_outputs[i].weightA = constrain(pc.weight_a, -100, 100);
        g_outputs[i].weightB = constrain(pc.weight_b, -100, 100);
        g_outputs[i].weightC = constrain(pc.weight_c, -100, 100);
        g_outputs[i].offsetA = constrain(pc.offset_a, -100, 100);
        g_outputs[i].offsetB = constrain(pc.offset_b, -100, 100);
        g_outputs[i].offsetC = constrain(pc.offset_c, -100, 100);
        g_outputs[i].thresholdPercent = constrain(pc.threshold, 0, 100);
        memcpy(g_outputs[i].name, pc.name, sizeof(g_outputs[i].name));
        g_outputs[i].name[sizeof(g_outputs[i].name) - 1] = '\0';
        if (g_outputs[i].type == ChannelType::Pwm && !ESP32PWM::hasPwm(g_outputs[i].pin)) {
            const int fallbackPin = firstAvailablePwmPin(i);
            if (fallbackPin < 0) {
                restoreOld();
                if (errorOut) {
                    *errorOut = "Invalid preset: no PWM-compatible GPIO available";
                }
                return false;
            }
#if RCCTL_DEBUG_LOG
            Console.printf("Preset pin remap: CH%d GPIO %d -> GPIO %d (PWM incompatible)\n", i, g_outputs[i].pin, fallbackPin);
#endif
            g_outputs[i].pin = static_cast<uint8_t>(fallbackPin);
        }

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

String mixModeLabel(MixMode mode) {
    return mode == MixMode::Multiply ? "Multiply" : "Add";
}

}  // namespace rcctl
