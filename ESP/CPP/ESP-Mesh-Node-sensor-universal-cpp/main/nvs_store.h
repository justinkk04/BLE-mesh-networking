#ifndef NVS_STORE_H
#define NVS_STORE_H

#include "nvs_flash.h"
#include "ble_mesh_example_nvs.h"

#ifdef __cplusplus
extern "C" {
#endif

// NVS handle — opened in app_main(), used by save/restore functions
extern nvs_handle_t NVS_HANDLE;

// Save current mesh node state to NVS
void save_node_state(void);

// Restore mesh node state from NVS. Updates cached_net_idx/app_idx.
void restore_node_state(void);

#ifdef __cplusplus
}
#endif

#endif // NVS_STORE_H
