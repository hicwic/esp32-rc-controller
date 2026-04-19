#pragma once

#include <Arduino.h>
#include <Bluepad32.h>
#include <ESP32Servo.h>

#include "control_inputs.h"
#include "preset_store.h"

namespace rcctl {

enum class InputType : uint8_t {
    Direct = 0,
    Toggle2Pos = 1,
    Toggle3Pos = 2,
};

enum class ModifierFunction : uint8_t {
    None = 0,
    Reverse = 1,
    Center = 2,
};

struct VirtualInputConfig {
    bool used = false;
    InputId primary = InputId::None;
    InputId secondary = InputId::None;
    InputType inputType = InputType::Direct;
    InputId modifier = InputId::None;
    ModifierFunction modifierFunction = ModifierFunction::None;
    int deadzonePercent = 10;
    int expoPercent = 0;
    char name[24] = "";
};

struct OutputChannelConfig {
    bool used = false;
    ChannelType type = ChannelType::Pwm;
    uint8_t pin = 0;
    bool inverted = false;
    int thresholdPercent = 50;
    int8_t sourceA = -1;
    int8_t sourceB = -1;
    int8_t sourceC = -1;
    int8_t weightA = 100;
    int8_t weightB = 0;
    int8_t weightC = 0;
    char name[24] = "";
};

VirtualInputConfig* virtualInputs();
OutputChannelConfig* outputs();
float* virtualRuntimeValues();
float* outputRuntimeValues();
bool* pwmAttached();

int firstFreeVirtualIndex();
int firstFreeOutputIndex();
bool outputPinAlreadyUsed(uint8_t pin, int ignoreIndex);

float evaluateVirtualInput(const VirtualInputConfig& in, ControllerPtr ctl);
float evaluateOutputSignal(const OutputChannelConfig& out);
void evaluateVirtualRuntime(ControllerPtr ctl);
void processGamepadToOutputs(ControllerPtr ctl);

void releaseOutputHardware(int index);
bool setupOutputHardware(int index, String* error);
void writeFailsafeForOutput(int index);
void applyFailsafeAllOutputs();

bool exportCurrentConfig(PersistedConfig* out);
bool applyPersistedConfig(const PersistedConfig& cfg, String* errorOut = nullptr);
bool saveRuntimeConfigToNvs();

String outputTypeLabel(ChannelType type);

}  // namespace rcctl
