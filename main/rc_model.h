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

enum class ToggleRangeMode : uint8_t {
    Unipolar = 0,
    Bipolar = 1,
};

enum class ModifierFunction : uint8_t {
    None = 0,
    Reverse = 1,
    Center = 2,
    Activate = 3,
    Desactivate = 4,
};

enum class MixMode : uint8_t {
    Add = 0,
    Multiply = 1,
};

enum class PwmFailsafeMode : uint8_t {
    Min = 0,
    Center = 1,
    Max = 2,
};

struct VirtualInputConfig {
    bool used = false;
    InputId primary = InputId::None;
    InputId secondary = InputId::None;
    InputType inputType = InputType::Direct;
    ToggleRangeMode toggleRange = ToggleRangeMode::Bipolar;
    bool rumbleEnabled = false;
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
    PwmFailsafeMode pwmFailsafeMode = PwmFailsafeMode::Center;
    int8_t sourceA = -1;
    int8_t sourceB = -1;
    int8_t sourceC = -1;
    MixMode mixMode = MixMode::Multiply;
    int8_t weightA = 100;
    int8_t weightB = 0;
    int8_t weightC = 0;
    int8_t offsetA = 0;
    int8_t offsetB = 0;
    int8_t offsetC = 0;
    char name[24] = "";
};

// Shared runtime/config arrays used by web handlers and control loop.
VirtualInputConfig* virtualInputs();
OutputChannelConfig* outputs();
float* virtualRuntimeValues();
float* outputRuntimeValues();
bool* pwmAttached();

// Allocation helpers for UI actions.
int firstFreeVirtualIndex();
int firstFreeOutputIndex();
bool outputPinAlreadyUsed(uint8_t pin, int ignoreIndex);

// Signal evaluation pipeline.
float evaluateVirtualInput(const VirtualInputConfig& in, ControllerPtr ctl);
float evaluateOutputSignal(const OutputChannelConfig& out);
void evaluateVirtualRuntime(ControllerPtr ctl);
void processGamepadToOutputs(ControllerPtr ctl);

// Hardware lifecycle for channel outputs.
void releaseOutputHardware(int index);
bool setupOutputHardware(int index, String* error);
bool rebuildOutputHardware(String* error);
void writeFailsafeForOutput(int index);
void applyFailsafeAllOutputs();

// Serialization and config-apply helpers.
bool exportCurrentConfig(PersistedConfig* out);
bool applyPersistedConfig(const PersistedConfig& cfg, String* errorOut = nullptr);
bool saveRuntimeConfigToNvs();

String outputTypeLabel(ChannelType type);
String mixModeLabel(MixMode mode);

}  // namespace rcctl
