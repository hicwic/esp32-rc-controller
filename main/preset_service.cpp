#include "preset_service.h"

#include "rc_model.h"

namespace rcctl {

bool saveUserPresetFromConfig(const String& rawName, const PersistedConfig& sourceCfg, String* errorOut) {
    return saveUserPresetBlob(rawName, &sourceCfg, errorOut);
}

bool saveCurrentAsUserPreset(const String& rawName, String* errorOut) {
    PersistedConfig cfg = {};
    exportCurrentConfig(&cfg);
    return saveUserPresetBlob(rawName, &cfg, errorOut);
}

bool presetNameExists(const String& rawName) {
    PresetDirectory dir = {};
    loadPresetDirectory(&dir);
    const String target = sanitizePresetName(rawName);
    for (int i = 0; i < static_cast<int>(dir.count); ++i) {
        if (target.equalsIgnoreCase(String(dir.names[i]))) {
            return true;
        }
    }
    return false;
}

String nextAvailableCustomModelName(const String& baseName) {
    const String base = sanitizePresetName(baseName);
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

}  // namespace rcctl

