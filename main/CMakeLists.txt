set(srcs
        "main.c"
        "control_inputs.cpp"
        "rc_model.cpp"
        "preset_store.cpp"
        "sketch.cpp"
        "web_ui.cpp")

set(requires "bluepad32" "bluepad32_arduino" "arduino" "btstack" "ESP32Servo")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
                    REQUIRES "${requires}")
