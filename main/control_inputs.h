#pragma once

#include <Arduino.h>
#include <Bluepad32.h>

enum class InputKind : uint8_t {
    Centered = 0,
    Positive = 1,
    Digital = 2,
};

enum class InputId : uint8_t {
    None = 0,
    AxisX,
    AxisY,
    AxisRX,
    AxisRY,
    Throttle,
    Brake,
    ButtonA,
    ButtonB,
    ButtonX,
    ButtonY,
    ButtonL1,
    ButtonR1,
    ButtonL2,
    ButtonR2,
    ButtonStart,
    ButtonSelect,
};

struct InputDefinition {
    InputId id;
    const char* label;
    InputKind kind;
};

extern const InputDefinition kInputs[];
extern const size_t kInputCount;

const InputDefinition* getInputDefinition(InputId id);
float normalizedForInput(InputId id, ControllerPtr ctl);
bool isInputActive(InputId id, ControllerPtr ctl);
InputId detectDominantInput(ControllerPtr ctl);

