#pragma once

#include <Arduino.h>

#include "control_inputs.h"

namespace rcctl {

constexpr int kMaxChannels = 24;
constexpr int kMaxUserPresets = 8;
constexpr uint32_t kConfigMagic = 0x52434346;  // "RCCF"
constexpr uint16_t kConfigVersion = 2;
constexpr uint16_t kPresetDirectoryVersion = 2;
constexpr const char* kPresetRcCar = "car";
constexpr const char* kPresetExcavator = "excavator";

enum class ChannelType : uint8_t {
    Pwm = 0,
    Switch = 1,
};

struct PersistedChannel {
    uint8_t used;
    uint8_t type;
    uint8_t pin;
    uint8_t inverted;
    uint8_t input;
    uint8_t input_secondary;
    uint8_t input_modifier;
    uint8_t modifier_reverses;
    uint8_t threshold;
    char name[24];
};

struct PersistedConfig {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    PersistedChannel channels[kMaxChannels];
};

struct PresetDirectory {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    char names[kMaxUserPresets][24];
};

String sanitizePresetName(const String& in);
bool loadPresetDirectory(PresetDirectory* out);
bool savePresetDirectory(const PresetDirectory& dir);
bool saveUserPresetBlob(const String& rawName, const PersistedConfig* sourceCfg, String* errorOut = nullptr);
bool loadUserPreset(const String& rawName, PersistedConfig* out, String* errorOut = nullptr);
bool deleteUserPreset(const String& rawName, String* errorOut = nullptr);
void buildBuiltinPresetRcCar(PersistedConfig* cfg);
void buildBuiltinPresetExcavator(PersistedConfig* cfg);
bool loadAnyPreset(const String& name, PersistedConfig* out, String* errorOut = nullptr);
bool saveBootModelName(const String& rawName);
String loadBootModelName();

}  // namespace rcctl
