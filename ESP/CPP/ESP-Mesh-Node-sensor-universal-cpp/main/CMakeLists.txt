set(srcs
    "main.cpp"
    "sensor.cpp"
    "load_control.cpp"
    "nvs_store.cpp"
    "command.cpp"
    "mesh_node.c"
    "gatt_service.cpp"
    "command_parser.cpp"
    "node_tracker.cpp"
    "monitor.cpp"
)

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS "."
                    REQUIRES bt nvs_flash esp_wifi driver)

# Suppress C-style cast warnings that originate from NimBLE/BLE Mesh C headers
# included in C++ translation units.
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-old-style-cast>
    $<$<COMPILE_LANGUAGE:CXX>:-Wno-missing-field-initializers>
)

# mesh_node.c stays as C (not C++) because esp_ble_mesh_defs.h pulls in the
# internal ESP-IDF header net.h which uses nested designated initializers
# (.ctx.recv_rssi) — a C99 GNU extension that is a hard syntax error in C++.
