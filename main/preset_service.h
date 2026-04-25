#pragma once

#include <Arduino.h>

#include "preset_store.h"

namespace rcctl {

// Saves a user preset from an explicit persisted snapshot.
bool saveUserPresetFromConfig(const String& rawName, const PersistedConfig& sourceCfg, String* errorOut = nullptr);
// Saves a user preset from the current in-memory runtime configuration.
bool saveCurrentAsUserPreset(const String& rawName, String* errorOut = nullptr);
// Case-insensitive name lookup in the user preset directory.
bool presetNameExists(const String& rawName);
// Returns a collision-free name by adding a numeric suffix when needed.
String nextAvailableCustomModelName(const String& baseName);

}  // namespace rcctl
