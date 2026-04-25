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

// Returns static metadata for an input identifier, or nullptr if unknown.
const InputDefinition* getInputDefinition(InputId id);
// Converts a raw gamepad input into a normalized scalar:
// - centered axes in [-1..1]
// - triggers and digital inputs in [0..1]
float normalizedForInput(InputId id, ControllerPtr ctl);
// Applies a generic "active" threshold depending on input kind.
bool isInputActive(InputId id, ControllerPtr ctl);
// Picks the strongest currently active control for input-learning UX.
InputId detectDominantInput(ControllerPtr ctl);
