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
                *errorOut = "Preset limit reached";
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
            *errorOut = "NVS unavailable";
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
            *errorOut = "Failed to save preset";
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
            *errorOut = "Unknown preset";
        }
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READONLY, &handle);
    if (err != ESP_OK) {
        if (errorOut) {
            *errorOut = "NVS unavailable";
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
            *errorOut = "Preset corrupted";
        }
        return false;
    }
    if (cfg.magic != kConfigMagic) {
        if (errorOut) {
            *errorOut = "Preset corrupted (invalid magic)";
        }
        return false;
    }
    if (cfg.version != kConfigVersion) {
        if (errorOut) {
            *errorOut = "Preset obsolete (format changed after update)";
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
            *errorOut = "Unknown preset";
        }
        return false;
    }

    nvs_handle_t handle = 0;
    esp_err_t err = nvs_open("rcctl", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        if (errorOut) {
            *errorOut = "NVS unavailable";
        }
        return false;
    }

    // Rebuild directory/blob slots by copying only valid presets, skipping:
    // - target preset to delete
    // - corrupted blobs (best effort cleanup in dev mode)
    PresetDirectory newDir = {};
    newDir.magic = kConfigMagic;
    newDir.version = kPresetDirectoryVersion;
    newDir.count = 0;

    const int oldCount = static_cast<int>(dir.count);
    for (int i = 0; i < oldCount; ++i) {
        if (i == slot) {
            continue;
        }

        PersistedConfig cfg = {};
        size_t len = sizeof(cfg);
        String srcKey = presetBlobKey(i);
        err = nvs_get_blob(handle, srcKey.c_str(), &cfg, &len);
        if (err != ESP_OK || len != sizeof(cfg)) {
            // Corrupted slot: skip it.
            continue;
        }

        const int dst = static_cast<int>(newDir.count);
        if (dst >= kMaxUserPresets) {
            break;
        }
        String dstKey = presetBlobKey(dst);
        err = nvs_set_blob(handle, dstKey.c_str(), &cfg, sizeof(cfg));
        if (err != ESP_OK) {
            nvs_close(handle);
            if (errorOut) {
                *errorOut = "Failed to delete preset";
            }
            return false;
        }

        strncpy(newDir.names[dst], dir.names[i], sizeof(newDir.names[dst]) - 1);
        newDir.names[dst][sizeof(newDir.names[dst]) - 1] = '\0';
        newDir.count++;
    }

    // Erase leftover old blob slots.
    for (int i = static_cast<int>(newDir.count); i < oldCount; ++i) {
        String key = presetBlobKey(i);
        nvs_erase_key(handle, key.c_str());
    }

    err = nvs_set_blob(handle, "preset_dir", &newDir, sizeof(newDir));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (err != ESP_OK) {
        if (errorOut) {
            *errorOut = "Failed to delete preset";
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
    cfg->virtual_inputs[0].used = 1;
    cfg->virtual_inputs[0].input = static_cast<uint8_t>(InputId::AxisRX);
    cfg->virtual_inputs[0].deadzone = 10;
    cfg->virtual_inputs[0].expo = 20;
    strncpy(cfg->virtual_inputs[0].name, "Steering", sizeof(cfg->virtual_inputs[0].name) - 1);

    cfg->virtual_inputs[1].used = 1;
    cfg->virtual_inputs[1].input = static_cast<uint8_t>(InputId::Throttle);
    cfg->virtual_inputs[1].input_secondary = static_cast<uint8_t>(InputId::Brake);
    cfg->virtual_inputs[1].deadzone = 0;
    cfg->virtual_inputs[1].expo = 25;
    strncpy(cfg->virtual_inputs[1].name, "Throttle", sizeof(cfg->virtual_inputs[1].name) - 1);

    cfg->virtual_inputs[2].used = 1;
    cfg->virtual_inputs[2].input = static_cast<uint8_t>(InputId::ButtonA);
    cfg->virtual_inputs[2].reserved = 34;  // input_type=Toggle3Pos(2), range=Unipolar(0), rumble=1
    cfg->virtual_inputs[2].deadzone = 0;
    cfg->virtual_inputs[2].expo = 0;
    strncpy(cfg->virtual_inputs[2].name, "Steering DR", sizeof(cfg->virtual_inputs[2].name) - 1);

    cfg->channels[0].used = 1;
    cfg->channels[0].type = static_cast<uint8_t>(ChannelType::Pwm);
    cfg->channels[0].pin = 13;
    cfg->channels[0].source_a = 0;
    cfg->channels[0].source_b = -1;
    cfg->channels[0].source_c = -1;
    cfg->channels[0].mix_mode = 1;  // Multiply
    cfg->channels[0].weight_a = 100;
    cfg->channels[0].weight_b = 0;
    cfg->channels[0].weight_c = 0;
    cfg->channels[0].offset_b = 0;
    cfg->channels[0].threshold = 50;
    strncpy(cfg->channels[0].name, "Steering", sizeof(cfg->channels[0].name) - 1);

    cfg->channels[1].used = 1;
    cfg->channels[1].type = static_cast<uint8_t>(ChannelType::Pwm);
    cfg->channels[1].pin = 12;
    cfg->channels[1].inverted = 1;
    cfg->channels[1].source_a = 1;
    cfg->channels[1].source_b = 2;
    cfg->channels[1].source_c = -1;
    cfg->channels[1].mix_mode = 1;  // Multiply
    cfg->channels[1].weight_a = 100;
    cfg->channels[1].weight_b = -75;
    cfg->channels[1].weight_c = 0;
    cfg->channels[1].offset_b = 100;
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
        InputId input;
        InputId secondary;
        uint8_t deadzone;
        uint8_t expo;
    } vmap[] = {{"Drive", InputId::AxisY, InputId::None, 10, 30},
                {"Steer", InputId::AxisX, InputId::None, 10, 20},
                {"Turret", InputId::AxisRX, InputId::None, 10, 15},
                {"Boom", InputId::AxisRY, InputId::None, 10, 20},
                {"Bucket", InputId::Throttle, InputId::Brake, 5, 25}};
    for (size_t i = 0; i < sizeof(vmap) / sizeof(vmap[0]); ++i) {
        cfg->virtual_inputs[i].used = 1;
        cfg->virtual_inputs[i].input = static_cast<uint8_t>(vmap[i].input);
        cfg->virtual_inputs[i].input_secondary = static_cast<uint8_t>(vmap[i].secondary);
        cfg->virtual_inputs[i].deadzone = vmap[i].deadzone;
        cfg->virtual_inputs[i].expo = vmap[i].expo;
        strncpy(cfg->virtual_inputs[i].name, vmap[i].name, sizeof(cfg->virtual_inputs[i].name) - 1);
    }

    const struct {
        const char* name;
        uint8_t pin;
        bool inverted;
        int8_t sourceA;
        int8_t weightA;
        int8_t sourceB;
        int8_t weightB;
    } omap[] = {{"Left Track", 5, false, 0, 100, 1, 100},
                {"Right Track", 6, false, 0, 100, 1, -100},
                {"Turret", 4, false, 2, 100, -1, 0},
                {"Boom", 7, false, 3, 100, -1, 0},
                {"Bucket", 16, true, 4, 100, -1, 0}};

    for (size_t i = 0; i < sizeof(omap) / sizeof(omap[0]); ++i) {
        cfg->channels[i].used = 1;
        cfg->channels[i].type = static_cast<uint8_t>(ChannelType::Pwm);
        cfg->channels[i].pin = omap[i].pin;
        cfg->channels[i].inverted = omap[i].inverted ? 1 : 0;
        cfg->channels[i].source_a = omap[i].sourceA;
        cfg->channels[i].weight_a = omap[i].weightA;
        cfg->channels[i].source_b = omap[i].sourceB;
        cfg->channels[i].weight_b = omap[i].weightB;
        cfg->channels[i].source_c = -1;
        cfg->channels[i].weight_c = 0;
        cfg->channels[i].threshold = 50;
        strncpy(cfg->channels[i].name, omap[i].name, sizeof(cfg->channels[i].name) - 1);
    }
}

void buildBuiltinPresetSkidSteer(PersistedConfig* cfg) {
    if (!cfg) {
        return;
    }
    *cfg = {};
    cfg->magic = kConfigMagic;
    cfg->version = kConfigVersion;

    const struct {
        const char* name;
        InputId input;
        InputId secondary;
        uint8_t deadzone;
        uint8_t expo;
    } vmap[] = {{"Drive", InputId::AxisY, InputId::None, 10, 20},
                {"Steer", InputId::AxisX, InputId::None, 10, 20},
                {"Arm", InputId::AxisRY, InputId::None, 10, 20},
                {"Bucket", InputId::AxisRX, InputId::None, 10, 20}};

    for (size_t i = 0; i < sizeof(vmap) / sizeof(vmap[0]); ++i) {
        cfg->virtual_inputs[i].used = 1;
        cfg->virtual_inputs[i].input = static_cast<uint8_t>(vmap[i].input);
        cfg->virtual_inputs[i].input_secondary = static_cast<uint8_t>(vmap[i].secondary);
        cfg->virtual_inputs[i].deadzone = vmap[i].deadzone;
        cfg->virtual_inputs[i].expo = vmap[i].expo;
        strncpy(cfg->virtual_inputs[i].name, vmap[i].name, sizeof(cfg->virtual_inputs[i].name) - 1);
    }

    const struct {
        const char* name;
        uint8_t pin;
        bool inverted;
        int8_t sourceA;
        int8_t weightA;
        int8_t sourceB;
        int8_t weightB;
    } omap[] = {{"Left Track", 5, false, 0, 100, 1, 100},
                {"Right Track", 6, false, 0, 100, 1, -100},
                {"Arm", 7, false, 2, 100, -1, 0},
                {"Bucket", 16, false, 3, 100, -1, 0}};

    for (size_t i = 0; i < sizeof(omap) / sizeof(omap[0]); ++i) {
        cfg->channels[i].used = 1;
        cfg->channels[i].type = static_cast<uint8_t>(ChannelType::Pwm);
        cfg->channels[i].pin = omap[i].pin;
        cfg->channels[i].inverted = omap[i].inverted ? 1 : 0;
        cfg->channels[i].source_a = omap[i].sourceA;
        cfg->channels[i].weight_a = omap[i].weightA;
        cfg->channels[i].source_b = omap[i].sourceB;
        cfg->channels[i].weight_b = omap[i].weightB;
        cfg->channels[i].source_c = -1;
        cfg->channels[i].weight_c = 0;
        cfg->channels[i].threshold = 50;
        strncpy(cfg->channels[i].name, omap[i].name, sizeof(cfg->channels[i].name) - 1);
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
    if (name == kPresetSkidSteer) {
        buildBuiltinPresetSkidSteer(out);
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
