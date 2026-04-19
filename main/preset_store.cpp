#include "preset_store.h"

#include <nvs.h>

namespace rcctl {

namespace {

String presetBlobKey(int slot) {
    return String("pr") + String(slot);
}

int findPresetSlotByName(const PresetDirectory& dir, const String& name) {
    for (int i = 0; i < static_cast<int>(dir.count); ++i) {
        if (name.equalsIgnoreCase(String(dir.names[i]))) {
            return i;
        }
    }
    return -1;
}

}  // namespace

String sanitizePresetName(const String& in) {
    String s = in;
    s.trim();
    String out;
    for (size_t i = 0; i < s.length() && out.length() < 23; ++i) {
        const char c = s[i];
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (ok) {
            out += c;
        } else if (c == ' ') {
            out += '_';
        }
    }
    if (out.isEmpty()) {
        out = "preset";
    }
    return out;
}

bool loadPresetDirectory(PresetDirectory* out) {
    if (!out) {
        return false;
    }
    PresetDirectory dir = {};
    dir.magic = kConfigMagic;
    dir.version = kPresetDirectoryVersion;

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        *out = dir;
        return true;
    }

    size_t len = sizeof(dir);
    err = nvs_get_blob(handle, "preset_dir", &dir, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(dir) || dir.magic != kConfigMagic || dir.version != kPresetDirectoryVersion) {
        PresetDirectory fresh = {};
        fresh.magic = kConfigMagic;
        fresh.version = kPresetDirectoryVersion;
        *out = fresh;
        return true;
    }

    if (dir.count > kMaxUserPresets) {
        dir.count = kMaxUserPresets;
    }
    *out = dir;
    return true;
}

bool savePresetDirectory(const PresetDirectory& dir) {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }
    err = nvs_set_blob(handle, "preset_dir", &dir, sizeof(dir));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

bool saveUserPresetBlob(const String& rawName, const PersistedConfig* sourceCfg, String* errorOut) {
    const String name = sanitizePresetName(rawName);
    if (!sourceCfg) {
        if (errorOut) {
            *errorOut = "Invalid source preset";
        }
        return false;
    }

    PresetDirectory dir = {};
    loadPresetDirectory(&dir);
    int slot = findPresetSlotByName(dir, name);
    if (slot < 0) {
        if (dir.count >= kMaxUserPresets) {
            if (errorOut) {
                *errorOut = "Limite presets atteinte";
            }
            return false;
        }
        slot = dir.count++;
        memset(dir.names[slot], 0, sizeof(dir.names[slot]));
        name.toCharArray(dir.names[slot], sizeof(dir.names[slot]));
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        if (errorOut) {
            *errorOut = "NVS indisponible";
        }
        return false;
    }
    String key = presetBlobKey(slot);
    err = nvs_set_blob(handle, key.c_str(), sourceCfg, sizeof(*sourceCfg));
    if (err == ESP_OK) {
        err = nvs_set_blob(handle, "preset_dir", &dir, sizeof(dir));
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        if (errorOut) {
            *errorOut = "Echec sauvegarde preset";
        }
        return false;
    }
    return true;
}

bool loadUserPreset(const String& rawName, PersistedConfig* out, String* errorOut) {
    if (!out) {
        return false;
    }
    const String name = sanitizePresetName(rawName);
    PresetDirectory dir = {};
    loadPresetDirectory(&dir);
    const int slot = findPresetSlotByName(dir, name);
    if (slot < 0) {
        if (errorOut) {
            *errorOut = "Preset inconnu";
        }
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (errorOut) {
            *errorOut = "NVS indisponible";
        }
        return false;
    }
    PersistedConfig cfg = {};
    size_t len = sizeof(cfg);
    String key = presetBlobKey(slot);
    err = nvs_get_blob(handle, key.c_str(), &cfg, &len);
    nvs_close(handle);
    if (err != ESP_OK || len != sizeof(cfg)) {
        if (errorOut) {
            *errorOut = "Preset corrompu";
        }
        return false;
    }
    *out = cfg;
    return true;
}

bool deleteUserPreset(const String& rawName, String* errorOut) {
    const String name = sanitizePresetName(rawName);
    PresetDirectory dir = {};
    loadPresetDirectory(&dir);
    const int slot = findPresetSlotByName(dir, name);
    if (slot < 0) {
        if (errorOut) {
            *errorOut = "Preset inconnu";
        }
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        if (errorOut) {
            *errorOut = "NVS indisponible";
        }
        return false;
    }

    for (int i = slot; i < static_cast<int>(dir.count) - 1; ++i) {
        PersistedConfig cfg = {};
        size_t len = sizeof(cfg);
        String srcKey = presetBlobKey(i + 1);
        err = nvs_get_blob(handle, srcKey.c_str(), &cfg, &len);
        if (err != ESP_OK || len != sizeof(cfg)) {
            nvs_close(handle);
            if (errorOut) {
                *errorOut = "Preset corrompu";
            }
            return false;
        }
        String dstKey = presetBlobKey(i);
        err = nvs_set_blob(handle, dstKey.c_str(), &cfg, sizeof(cfg));
        if (err != ESP_OK) {
            nvs_close(handle);
            if (errorOut) {
                *errorOut = "Echec suppression preset";
            }
            return false;
        }
        strncpy(dir.names[i], dir.names[i + 1], sizeof(dir.names[i]) - 1);
        dir.names[i][sizeof(dir.names[i]) - 1] = '\0';
    }

    if (dir.count > 0) {
        const int last = static_cast<int>(dir.count) - 1;
        String lastKey = presetBlobKey(last);
        nvs_erase_key(handle, lastKey.c_str());
        memset(dir.names[last], 0, sizeof(dir.names[last]));
        dir.count--;
    }

    err = nvs_set_blob(handle, "preset_dir", &dir, sizeof(dir));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        if (errorOut) {
            *errorOut = "Echec suppression preset";
        }
        return false;
    }
    return true;
}

void buildBuiltinPresetRcCar(PersistedConfig* cfg) {
    if (!cfg) {
        return;
    }
    *cfg = {};
    cfg->magic = kConfigMagic;
    cfg->version = kConfigVersion;
    cfg->channels[0].used = 1;
    cfg->channels[0].type = static_cast<uint8_t>(ChannelType::Pwm);
    cfg->channels[0].pin = 13;
    cfg->channels[0].inverted = 0;
    cfg->channels[0].input = static_cast<uint8_t>(InputId::AxisX);
    cfg->channels[0].threshold = 50;
    strncpy(cfg->channels[0].name, "Steering", sizeof(cfg->channels[0].name) - 1);

    cfg->channels[1].used = 1;
    cfg->channels[1].type = static_cast<uint8_t>(ChannelType::Pwm);
    cfg->channels[1].pin = 12;
    cfg->channels[1].inverted = 1;
    cfg->channels[1].input = static_cast<uint8_t>(InputId::Throttle);
    cfg->channels[1].input_secondary = static_cast<uint8_t>(InputId::Brake);
    cfg->channels[1].threshold = 50;
    strncpy(cfg->channels[1].name, "Throttle", sizeof(cfg->channels[1].name) - 1);
}

void buildBuiltinPresetExcavator(PersistedConfig* cfg) {
    if (!cfg) {
        return;
    }
    *cfg = {};
    cfg->magic = kConfigMagic;
    cfg->version = kConfigVersion;

    const struct {
        const char* name;
        uint8_t pin;
        InputId input;
        InputId secondary;
        InputId modifier;
        bool modifierReverses;
        bool inverted;
    } map[] = {{"Turret", 4, InputId::AxisRX, InputId::None, InputId::None, false, false},
               {"Left Track", 5, InputId::Brake, InputId::None, InputId::ButtonL1, true, false},
               {"Right Track", 6, InputId::Throttle, InputId::None, InputId::ButtonR1, true, false},
               {"Boom", 7, InputId::AxisX, InputId::None, InputId::None, false, false},
               {"Stick", 15, InputId::AxisY, InputId::None, InputId::None, false, false},
               {"Bucket", 16, InputId::AxisRY, InputId::None, InputId::None, false, true}};

    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); ++i) {
        cfg->channels[i].used = 1;
        cfg->channels[i].type = static_cast<uint8_t>(ChannelType::Pwm);
        cfg->channels[i].pin = map[i].pin;
        cfg->channels[i].inverted = map[i].inverted ? 1 : 0;
        cfg->channels[i].input = static_cast<uint8_t>(map[i].input);
        cfg->channels[i].input_secondary = static_cast<uint8_t>(map[i].secondary);
        cfg->channels[i].input_modifier = static_cast<uint8_t>(map[i].modifier);
        cfg->channels[i].modifier_reverses = map[i].modifierReverses ? 1 : 0;
        cfg->channels[i].threshold = 50;
        strncpy(cfg->channels[i].name, map[i].name, sizeof(cfg->channels[i].name) - 1);
    }
}

bool loadAnyPreset(const String& name, PersistedConfig* out, String* errorOut) {
    if (name == "rc_car_controller") {
        buildBuiltinPresetRcCar(out);
        return true;
    }
    if (name == kPresetRcCar) {
        buildBuiltinPresetRcCar(out);
        return true;
    }
    if (name == kPresetExcavator) {
        buildBuiltinPresetExcavator(out);
        return true;
    }
    return loadUserPreset(name, out, errorOut);
}

bool saveBootModelName(const String& rawName) {
    const String name = sanitizePresetName(rawName);
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return false;
    }
    err = nvs_set_str(handle, "boot_model", name.c_str());
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    return err == ESP_OK;
}

String loadBootModelName() {
    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return String(kPresetRcCar);
    }
    size_t required = 0;
    err = nvs_get_str(handle, "boot_model", nullptr, &required);
    if (err != ESP_OK || required == 0 || required > 64) {
        nvs_close(handle);
        return String(kPresetRcCar);
    }
    char buf[64] = {0};
    err = nvs_get_str(handle, "boot_model", buf, &required);
    nvs_close(handle);
    if (err != ESP_OK) {
        return String(kPresetRcCar);
    }
    String name = sanitizePresetName(String(buf));
    if (name == "rc_car_controller") {
        name = String(kPresetRcCar);
    }
    return name;
}

}  // namespace rcctl
