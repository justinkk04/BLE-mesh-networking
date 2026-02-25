set(srcs
    "main.c"
    "sensor.c"
    "load_control.c"
    "nvs_store.c"
    "command.c"
    "mesh_node.c"
)

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ".")
