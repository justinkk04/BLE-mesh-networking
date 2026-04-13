# main/CMakeLists.txt
#
# Registers all C++ source files as a single ESP-IDF component.
# The include directory is "." so headers can be included by name only
# (e.g. #include "MeshConfig.h").

idf_component_register(
    SRCS
        "main.cpp"
        "MeshConfig.cpp"
        "NodeRegistry.cpp"
        "CompositionParser.cpp"
        "ModelBinder.cpp"
        "BLEMeshProvisioner.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        bt
        nvs_flash
)

# Enforce C++17 for this component (ESP-IDF default is C++17 with GNU extensions,
# but we declare it explicitly for clarity).
target_compile_options(${COMPONENT_LIB} PRIVATE
    -std=gnu++17
    -fno-exceptions    # ESP-IDF does not enable C++ exceptions by default
    -fno-rtti          # Disable RTTI to match ESP-IDF defaults
)
