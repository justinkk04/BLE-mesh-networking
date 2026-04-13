/* MeshConstants.h
 *
 * Compile-time constants for the BLE Mesh Provisioner.
 * Replaces the #define constants from mesh_config.h.
 */

#pragma once

#include <cstdint>

// Espressif company ID (used for vendor models)
static constexpr uint16_t CID_ESP             = 0x02E5;

// Vendor model IDs (shared with gateway and mesh node firmware)
static constexpr uint16_t VND_MODEL_ID_CLIENT = 0x0000;
static constexpr uint16_t VND_MODEL_ID_SERVER = 0x0001;

// Group address all configured nodes subscribe to
static constexpr uint16_t MESH_GROUP_ADDR     = 0xC000;

// Provisioner's own unicast address
static constexpr uint16_t PROV_OWN_ADDR       = 0x0001;

// Message defaults
static constexpr uint8_t  MSG_SEND_TTL        = 3;
static constexpr uint32_t MSG_TIMEOUT         = 0;

// Composition data page
static constexpr uint8_t  COMP_DATA_PAGE_0    = 0x00;

// Key indices and fill values
static constexpr uint16_t APP_KEY_IDX         = 0x0000;
static constexpr uint8_t  APP_KEY_OCTET       = 0x12;
static constexpr uint8_t  NET_KEY_OCTET       = 0x11;

// UUID prefix filter: only provision devices whose UUID starts with these bytes
static constexpr uint8_t  UUID_MATCH_LEN      = 2;
static constexpr uint8_t  UUID_MATCH_BYTE0    = 0xdd;
static constexpr uint8_t  UUID_MATCH_BYTE1    = 0xdd;
