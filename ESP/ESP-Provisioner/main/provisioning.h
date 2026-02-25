/* Provisioning: callbacks and mesh initialization */

#ifndef PROVISIONING_H
#define PROVISIONING_H

#include "mesh_config.h"
#include "node_registry.h"
#include "composition.h"
#include "model_binding.h"

esp_err_t prov_complete(int node_idx, const esp_ble_mesh_octet16_t uuid,
                        uint16_t unicast, uint8_t elem_num,
                        uint16_t net_idx);

void provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                     esp_ble_mesh_prov_cb_param_t *param);

void config_client_cb(esp_ble_mesh_cfg_client_cb_event_t event,
                      esp_ble_mesh_cfg_client_cb_param_t *param);

void generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                       esp_ble_mesh_generic_client_cb_param_t *param);

esp_err_t ble_mesh_init(void);

#endif /* PROVISIONING_H */
