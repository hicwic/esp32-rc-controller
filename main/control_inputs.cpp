#include "control_inputs.h"

#include <math.h>

namespace {

constexpr int kAxisMax = 512;
constexpr int kTriggerMin = 0;
constexpr int kTriggerMax = 1023;

}  // namespace

const InputDefinition kInputs[] = {
    {InputId::AxisX, "Axis X (left stick LR)", InputKind::Centered},
    {InputId::AxisY, "Axis Y (left stick UD)", InputKind::Centered},
    {InputId::AxisRX, "Axis RX (right stick LR)", InputKind::Centered},
    {InputId::AxisRY, "Axis RY (right stick UD)", InputKind::Centered},
    {InputId::Throttle, "RT trigger", InputKind::Positive},
    {InputId::Brake, "LT trigger", InputKind::Positive},
    {InputId::ButtonA, "Button A", InputKind::Digital},
    {InputId::ButtonB, "Button B", InputKind::Digital},
    {InputId::ButtonX, "Button X", InputKind::Digital},
    {InputId::ButtonY, "Button Y", InputKind::Digital},
    {InputId::ButtonL1, "Button LB", InputKind::Digital},
    {InputId::ButtonR1, "Button RB", InputKind::Digital},
    {InputId::ButtonL2, "Button L2", InputKind::Digital},
    {InputId::ButtonR2, "Button R2", InputKind::Digital},
    {InputId::ButtonStart, "Button Start", InputKind::Digital},
    {InputId::ButtonSelect, "Button Select", InputKind::Digital},
};

const size_t kInputCount = sizeof(kInputs) / sizeof(kInputs[0]);

const InputDefinition* getInputDefinition(InputId id) {
    for (const auto& def : kInputs) {
        if (def.id == id) {
            return &def;
        }
    }
    return nullptr;
}

float normalizedForInput(InputId id, ControllerPtr ctl) {
    if (!ctl) {
        return 0.0f;
    }

    switch (id) {
        case InputId::AxisX:
            return constrain(static_cast<float>(ctl->axisX()) / static_cast<float>(kAxisMax), -1.0f, 1.0f);
        case InputId::AxisY:
            return constrain(static_cast<float>(ctl->axisY()) / static_cast<float>(kAxisMax), -1.0f, 1.0f);
        case InputId::AxisRX:
            return constrain(static_cast<float>(ctl->axisRX()) / static_cast<float>(kAxisMax), -1.0f, 1.0f);
        case InputId::AxisRY:
            return constrain(static_cast<float>(ctl->axisRY()) / static_cast<float>(kAxisMax), -1.0f, 1.0f);
        case InputId::Throttle:
            return constrain(static_cast<float>(ctl->throttle() - kTriggerMin) / static_cast<float>(kTriggerMax), 0.0f, 1.0f);
        case InputId::Brake:
            return constrain(static_cast<float>(ctl->brake() - kTriggerMin) / static_cast<float>(kTriggerMax), 0.0f, 1.0f);
        case InputId::ButtonA:
            return ctl->a() ? 1.0f : 0.0f;
        case InputId::ButtonB:
            return ctl->b() ? 1.0f : 0.0f;
        case InputId::ButtonX:
            return ctl->x() ? 1.0f : 0.0f;
        case InputId::ButtonY:
            return ctl->y() ? 1.0f : 0.0f;
        case InputId::ButtonL1:
            return ctl->l1() ? 1.0f : 0.0f;
        case InputId::ButtonR1:
            return ctl->r1() ? 1.0f : 0.0f;
        case InputId::ButtonL2:
            return ctl->l2() ? 1.0f : 0.0f;
        case InputId::ButtonR2:
            return ctl->r2() ? 1.0f : 0.0f;
        case InputId::ButtonStart:
            return ctl->miscStart() ? 1.0f : 0.0f;
        case InputId::ButtonSelect:
            return ctl->miscSelect() ? 1.0f : 0.0f;
        default:
            return 0.0f;
    }
}

bool isInputActive(InputId id, ControllerPtr ctl) {
    if (id == InputId::None || !ctl) {
        return false;
    }
    const InputDefinition* def = getInputDefinition(id);
    if (!def) {
        return false;
    }
    float n = normalizedForInput(id, ctl);
    if (def->kind == InputKind::Digital) {
        return n >= 0.5f;
    }
    if (def->kind == InputKind::Centered) {
        n = fabsf(n);
    }
    return n >= 0.5f;
}

InputId detectDominantInput(ControllerPtr ctl) {
    if (!ctl) {
        return InputId::None;
    }

    // Ordered by interaction intent: buttons first, then analog controls.
    const struct {
        InputId id;
        float threshold;
        bool absValue;
    } candidates[] = {{InputId::ButtonA, 0.5f, false},   {InputId::ButtonB, 0.5f, false},
                      {InputId::ButtonX, 0.5f, false},   {InputId::ButtonY, 0.5f, false},
                      {InputId::ButtonL1, 0.5f, false},  {InputId::ButtonR1, 0.5f, false},
                      {InputId::ButtonStart, 0.5f, false}, {InputId::ButtonSelect, 0.5f, false},
                      {InputId::Throttle, 0.15f, false}, {InputId::Brake, 0.15f, false},
                      {InputId::AxisX, 0.25f, true},     {InputId::AxisY, 0.25f, true},
                      {InputId::AxisRX, 0.25f, true},    {InputId::AxisRY, 0.25f, true}};

    InputId best = InputId::None;
    float bestVal = 0.0f;
    for (const auto& c : candidates) {
        float v = normalizedForInput(c.id, ctl);
        if (c.absValue) {
            v = fabsf(v);
        }
        if (v >= c.threshold && v > bestVal) {
            bestVal = v;
            best = c.id;
        }
    }
    return best;
}
