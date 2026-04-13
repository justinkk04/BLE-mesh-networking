/* BLEMeshProvisioner.h
 *
 * Central class that initialises the ESP BLE Mesh stack and handles all
 * provisioning events.  Owns a NodeRegistry and a ModelBinder.
 *
 * The ESP-IDF BLE Mesh API requires plain C function pointers for callbacks,
 * so each callback is a static method that forwards to the singleton instance
 * via BLEMeshProvisioner::instance_.
 *
 * Typical usage (from main.cpp):
 *
 *   static BLEMeshProvisioner provisioner;
 *   provisioner.init();
 */

#pragma once

#include "MeshConfig.h"
#include "ModelBinder.h"
#include "NodeRegistry.h"
#include "esp_err.h"

#include "esp_ble_mesh_config_model_api.h"      // cfg_client_cb_param_t
#include "esp_ble_mesh_generic_model_api.h"    // generic_client_cb_param_t
#include "esp_ble_mesh_provisioning_api.h"     // prov_cb_param_t

class BLEMeshProvisioner {
public:
    BLEMeshProvisioner();

    // Initialise keys, register callbacks, start the mesh stack, and enable
    // the provisioner on both PB-ADV and PB-GATT bearers.
    esp_err_t init();

    // Accessors (mainly for unit testing)
    NodeRegistry& registry() { return registry_; }
    ModelBinder&  binder()   { return binder_; }

private:
    // -----------------------------------------------------------------------
    // Sub-event handlers (called from the static callbacks)
    // -----------------------------------------------------------------------

    // Dispatches an incoming unprovisioned device advertisement.
    void onUnprovAdvPacket(uint8_t               dev_uuid[16],
                           uint8_t               addr[BD_ADDR_LEN],
                           esp_ble_mesh_addr_type_t addr_type,
                           uint16_t              oob_info,
                           uint8_t               adv_type,
                           esp_ble_mesh_prov_bearer_t bearer);

    // Called when a node has been fully provisioned and received a unicast addr.
    esp_err_t onProvisioningComplete(int                        node_idx,
                                     const esp_ble_mesh_octet16_t uuid,
                                     uint16_t                   unicast,
                                     uint8_t                    elem_num,
                                     uint16_t                   net_idx);

    void onLinkOpen(esp_ble_mesh_prov_bearer_t bearer);
    void onLinkClose(esp_ble_mesh_prov_bearer_t bearer, uint8_t reason);

    // -----------------------------------------------------------------------
    // Static callbacks registered with ESP-IDF (forward to instance_)
    // -----------------------------------------------------------------------
    static void provisioningCallback(esp_ble_mesh_prov_cb_event_t    event,
                                     esp_ble_mesh_prov_cb_param_t*   param);

    static void configClientCallback(esp_ble_mesh_cfg_client_cb_event_t   event,
                                     esp_ble_mesh_cfg_client_cb_param_t*  param);

    static void genericClientCallback(esp_ble_mesh_generic_client_cb_event_t  event,
                                      esp_ble_mesh_generic_client_cb_param_t* param);

    // -----------------------------------------------------------------------
    // Data members
    // -----------------------------------------------------------------------
    NodeRegistry registry_;
    ModelBinder  binder_;

    // Singleton pointer so static callbacks can reach the live instance
    static BLEMeshProvisioner* instance_;
};
