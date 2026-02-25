set(srcs
    "main.c"
    "gatt_service.c"
    "mesh_gateway.c"
    "command_parser.c"
    "monitor.c"
    "node_tracker.c"
    "nvs_store.c"
)

idf_component_register(SRCS ${srcs}
                    INCLUDE_DIRS ".")
