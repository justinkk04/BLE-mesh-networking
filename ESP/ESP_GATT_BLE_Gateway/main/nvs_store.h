#ifndef NVS_STORE_H
#define NVS_STORE_H

#include "nvs_flash.h"
#include "ble_mesh_example_nvs.h"

struct gateway_state {
  uint16_t net_idx;
  uint16_t app_idx;
  uint16_t addr;
  uint8_t vnd_bound_flag; // Persisted: vendor model bound to AppKey
};

extern struct gateway_state gw_state;
extern nvs_handle_t NVS_HANDLE;

void save_gw_state(void);
void restore_gw_state(void);

#endif /* NVS_STORE_H */
