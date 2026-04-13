/* ModelBinder.h
 *
 * Drives the sequential model-configuration chain after a node is provisioned.
 *
 * Priority order executed by bindNextModel():
 *   1. Bind OnOff Server  to AppKey (if present and not yet bound)
 *   2. Bind OnOff Client  to AppKey (if present and not yet bound)
 *   3. Bind Vendor Server to AppKey (if present and not yet bound)
 *   4. Bind Vendor Client to AppKey (if present and not yet bound)
 *   5. Subscribe Vendor Server to MESH_GROUP_ADDR (if not yet subscribed)
 *   6. Log "FULLY CONFIGURED" when all steps are complete
 *
 * Each bind/subscribe call is async; the caller must invoke bindNextModel()
 * again from the config-client callback once the previous step confirms.
 */

#pragma once

#include "MeshNode.h"
#include "NodeRegistry.h"
#include "esp_err.h"

class ModelBinder {
public:
    explicit ModelBinder(NodeRegistry& registry);

    // Send a MODEL_APP_BIND for a SIG model (company_id = CID_NVAL).
    esp_err_t bindModel(MeshNode* node, uint16_t model_id);

    // Send a MODEL_APP_BIND for a vendor model (company_id = CID_ESP).
    esp_err_t bindVendorModel(MeshNode* node, uint16_t model_id);

    // Send a MODEL_SUB_ADD to subscribe the Vendor Server to MESH_GROUP_ADDR.
    esp_err_t subscribeVendorModelToGroup(MeshNode* node);

    // Drive the state machine: determine which step is next and send it.
    void bindNextModel(MeshNode* node);

private:
    NodeRegistry& registry_;
};
