set(srcs
        "main.c"
        "control_inputs.cpp"
        "json_utils.cpp"
        "state_service.cpp"
        "preset_service.cpp"
        "runtime_loop.cpp"
        "web_routes.cpp"
        "rc_model.cpp"
        "preset_store.cpp"
        "sketch.cpp"
        "web_ui.cpp")

set(requires "bluepad32" "bluepad32_arduino" "arduino" "btstack" "ESP32Servo" "spiffs")

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
                    REQUIRES "${requires}")
