/**
 * BLE Management Feature - Custom Studio RPC Handler
 *
 * This file implements BLE management functionality for ZMK Studio.
 * It provides APIs to:
 * - View and manage BLE profiles
 * - Set custom names for profiles (tied to BLE address)
 * - Switch active profiles
 * - Unpair profiles
 * - Manage split keyboard connections
 */

#include <pb_decode.h>
#include <pb_encode.h>
#include <zmk/studio/custom.h>
#include <zmk/ble_management/ble_management.pb.h>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/settings/settings.h>
#include <zmk/ble.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#include <zmk/split/bluetooth/central.h>
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_PERIPHERAL)
#include <zmk/split/bluetooth/peripheral.h>
#endif
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Settings namespace for storing profile names
#define SETTINGS_NAME_PREFIX "ble_mgmt/names/"

// Structure to store profile name tied to BLE address
struct profile_name_entry {
    bt_addr_le_t addr;
    char name[32];
};

// Profile names cache (in memory)
static struct profile_name_entry profile_names[ZMK_BLE_PROFILE_COUNT];

/**
 * Metadata for the custom subsystem.
 */
static struct zmk_rpc_custom_subsystem_meta ble_management_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS("http://localhost:5173"),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

/**
 * Register the custom RPC subsystem.
 */
ZMK_RPC_CUSTOM_SUBSYSTEM(zmk__ble_management, &ble_management_meta,
                         ble_management_rpc_handle_request);

ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(zmk__ble_management, zmk_ble_management_Response);

// Forward declarations
static int handle_get_profiles_request(const zmk_ble_management_GetProfilesRequest *req,
                                       zmk_ble_management_Response *resp);
static int handle_set_profile_name_request(const zmk_ble_management_SetProfileNameRequest *req,
                                           zmk_ble_management_Response *resp);
static int handle_switch_profile_request(const zmk_ble_management_SwitchProfileRequest *req,
                                         zmk_ble_management_Response *resp);
static int handle_unpair_profile_request(const zmk_ble_management_UnpairProfileRequest *req,
                                         zmk_ble_management_Response *resp);
static int handle_get_split_info_request(const zmk_ble_management_GetSplitInfoRequest *req,
                                         zmk_ble_management_Response *resp);
static int handle_forget_split_bond_request(const zmk_ble_management_ForgetSplitBondRequest *req,
                                            zmk_ble_management_Response *resp);

/**
 * Get profile name from cache based on BLE address
 */
static const char *get_profile_name(const bt_addr_le_t *addr) {
    if (!addr) {
        return "";
    }

    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        if (bt_addr_le_eq(&profile_names[i].addr, addr)) {
            return profile_names[i].name;
        }
    }
    return "";
}

/**
 * Save profile name to settings and cache
 */
static int save_profile_name(const bt_addr_le_t *addr, const char *name) {
    if (!addr || !name) {
        return -EINVAL;
    }

    // Find existing entry or empty slot
    int slot = -1;
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        if (bt_addr_le_eq(&profile_names[i].addr, addr)) {
            slot = i;
            break;
        }
        if (slot == -1 && bt_addr_le_eq(&profile_names[i].addr, BT_ADDR_LE_NONE)) {
            slot = i;
        }
    }

    if (slot == -1) {
        LOG_WRN("No slot available for profile name");
        return -ENOMEM;
    }

    // Update cache
    bt_addr_le_copy(&profile_names[slot].addr, addr);
    strncpy(profile_names[slot].name, name, sizeof(profile_names[slot].name) - 1);
    profile_names[slot].name[sizeof(profile_names[slot].name) - 1] = '\0';

    // Save to settings
    char setting_name[64];
    char addr_str[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
    snprintf(setting_name, sizeof(setting_name), SETTINGS_NAME_PREFIX "%s", addr_str);

    return settings_save_one(setting_name, name, strlen(name) + 1);
}

/**
 * Settings callback for loading profile names
 */
static int profile_names_settings_set(const char *name, size_t len, settings_read_cb read_cb,
                                     void *cb_arg) {
    const char *next;
    int rc;

    if (settings_name_steq(name, SETTINGS_NAME_PREFIX, &next) && next) {
        char addr_str[BT_ADDR_LE_STR_LEN];
        strncpy(addr_str, next, sizeof(addr_str) - 1);
        addr_str[sizeof(addr_str) - 1] = '\0';

        bt_addr_le_t addr;
        // Parse address (format: "XX:XX:XX:XX:XX:XX (type)")
        if (bt_addr_le_from_str(addr_str, "random", &addr) != 0 &&
            bt_addr_le_from_str(addr_str, "public", &addr) != 0) {
            LOG_WRN("Failed to parse address: %s", addr_str);
            return 0;
        }

        // Find or allocate slot
        int slot = -1;
        for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
            if (bt_addr_le_eq(&profile_names[i].addr, &addr)) {
                slot = i;
                break;
            }
            if (slot == -1 && bt_addr_le_eq(&profile_names[i].addr, BT_ADDR_LE_NONE)) {
                slot = i;
            }
        }

        if (slot == -1) {
            LOG_WRN("No slot for loading profile name");
            return 0;
        }

        bt_addr_le_copy(&profile_names[slot].addr, &addr);
        rc = read_cb(cb_arg, profile_names[slot].name, sizeof(profile_names[slot].name));
        if (rc >= 0) {
            profile_names[slot].name[sizeof(profile_names[slot].name) - 1] = '\0';
            LOG_DBG("Loaded profile name for %s: %s", addr_str, profile_names[slot].name);
        }
    }

    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(ble_mgmt, SETTINGS_NAME_PREFIX, NULL, profile_names_settings_set,
                              NULL, NULL);

/**
 * Main request handler for the custom RPC subsystem.
 */
static bool
ble_management_rpc_handle_request(const zmk_custom_CallRequest *raw_request,
                                  pb_callback_t *encode_response) {
    zmk_ble_management_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(zmk__ble_management, encode_response);

    zmk_ble_management_Request req = zmk_ble_management_Request_init_zero;

    // Decode the incoming request
    pb_istream_t req_stream =
        pb_istream_from_buffer(raw_request->payload.bytes, raw_request->payload.size);
    if (!pb_decode(&req_stream, zmk_ble_management_Request_fields, &req)) {
        LOG_WRN("Failed to decode ble_management request: %s", PB_GET_ERROR(&req_stream));
        zmk_ble_management_ErrorResponse err = zmk_ble_management_ErrorResponse_init_zero;
        snprintf(err.message, sizeof(err.message), "Failed to decode request");
        resp->which_response_type = zmk_ble_management_Response_error_tag;
        resp->response_type.error = err;
        return true;
    }

    int rc = 0;
    switch (req.which_request_type) {
    case zmk_ble_management_Request_get_profiles_tag:
        rc = handle_get_profiles_request(&req.request_type.get_profiles, resp);
        break;
    case zmk_ble_management_Request_set_profile_name_tag:
        rc = handle_set_profile_name_request(&req.request_type.set_profile_name, resp);
        break;
    case zmk_ble_management_Request_switch_profile_tag:
        rc = handle_switch_profile_request(&req.request_type.switch_profile, resp);
        break;
    case zmk_ble_management_Request_unpair_profile_tag:
        rc = handle_unpair_profile_request(&req.request_type.unpair_profile, resp);
        break;
    case zmk_ble_management_Request_get_split_info_tag:
        rc = handle_get_split_info_request(&req.request_type.get_split_info, resp);
        break;
    case zmk_ble_management_Request_forget_split_bond_tag:
        rc = handle_forget_split_bond_request(&req.request_type.forget_split_bond, resp);
        break;
    default:
        LOG_WRN("Unsupported request type: %d", req.which_request_type);
        rc = -ENOTSUP;
    }

    if (rc != 0) {
        zmk_ble_management_ErrorResponse err = zmk_ble_management_ErrorResponse_init_zero;
        snprintf(err.message, sizeof(err.message), "Failed to process request: %d", rc);
        resp->which_response_type = zmk_ble_management_Response_error_tag;
        resp->response_type.error = err;
    }
    return true;
}

/**
 * Handle GetProfilesRequest
 */
static int handle_get_profiles_request(const zmk_ble_management_GetProfilesRequest *req,
                                       zmk_ble_management_Response *resp) {
    LOG_DBG("GetProfilesRequest");

    zmk_ble_management_GetProfilesResponse result = zmk_ble_management_GetProfilesResponse_init_zero;
    result.max_profiles = ZMK_BLE_PROFILE_COUNT;

    int active = zmk_ble_active_profile_index();

    for (uint8_t i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        zmk_ble_management_ProfileInfo *profile = &result.profiles[i];
        profile->index = i;
        profile->is_open = zmk_ble_profile_is_open(i);
        profile->is_connected = zmk_ble_profile_is_connected(i);
        profile->is_active = (i == active);

        // Get BLE address
        bt_addr_le_t *addr = zmk_ble_profile_address(i);
        if (addr && !bt_addr_le_eq(addr, BT_ADDR_LE_NONE)) {
            char addr_str[BT_ADDR_LE_STR_LEN];
            bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
            strncpy(profile->address, addr_str, sizeof(profile->address) - 1);
            profile->address[sizeof(profile->address) - 1] = '\0';

            // Get custom name
            const char *name = get_profile_name(addr);
            if (name && name[0] != '\0') {
                strncpy(profile->name, name, sizeof(profile->name) - 1);
                profile->name[sizeof(profile->name) - 1] = '\0';
            }
        }
    }

    result.profiles_count = ZMK_BLE_PROFILE_COUNT;
    resp->which_response_type = zmk_ble_management_Response_get_profiles_tag;
    resp->response_type.get_profiles = result;
    return 0;
}

/**
 * Handle SetProfileNameRequest
 */
static int handle_set_profile_name_request(const zmk_ble_management_SetProfileNameRequest *req,
                                           zmk_ble_management_Response *resp) {
    LOG_DBG("SetProfileNameRequest: index=%d, name=%s", req->index, req->name);

    zmk_ble_management_SetProfileNameResponse result =
        zmk_ble_management_SetProfileNameResponse_init_zero;

    if (req->index >= ZMK_BLE_PROFILE_COUNT) {
        LOG_WRN("Invalid profile index: %d", req->index);
        result.success = false;
    } else {
        bt_addr_le_t *addr = zmk_ble_profile_address(req->index);
        if (addr && !bt_addr_le_eq(addr, BT_ADDR_LE_NONE)) {
            int rc = save_profile_name(addr, req->name);
            result.success = (rc == 0);
        } else {
            LOG_WRN("Profile %d has no address", req->index);
            result.success = false;
        }
    }

    resp->which_response_type = zmk_ble_management_Response_set_profile_name_tag;
    resp->response_type.set_profile_name = result;
    return 0;
}

/**
 * Handle SwitchProfileRequest
 */
static int handle_switch_profile_request(const zmk_ble_management_SwitchProfileRequest *req,
                                         zmk_ble_management_Response *resp) {
    LOG_DBG("SwitchProfileRequest: index=%d", req->index);

    zmk_ble_management_SwitchProfileResponse result =
        zmk_ble_management_SwitchProfileResponse_init_zero;

    if (req->index >= ZMK_BLE_PROFILE_COUNT) {
        LOG_WRN("Invalid profile index: %d", req->index);
        result.success = false;
    } else {
        int rc = zmk_ble_prof_select(req->index);
        result.success = (rc == 0);
    }

    resp->which_response_type = zmk_ble_management_Response_switch_profile_tag;
    resp->response_type.switch_profile = result;
    return 0;
}

/**
 * Handle UnpairProfileRequest
 */
static int handle_unpair_profile_request(const zmk_ble_management_UnpairProfileRequest *req,
                                         zmk_ble_management_Response *resp) {
    LOG_DBG("UnpairProfileRequest: index=%d", req->index);

    zmk_ble_management_UnpairProfileResponse result =
        zmk_ble_management_UnpairProfileResponse_init_zero;

    if (req->index >= ZMK_BLE_PROFILE_COUNT) {
        LOG_WRN("Invalid profile index: %d", req->index);
        result.success = false;
    } else {
        // Clear profile name from cache if it exists
        bt_addr_le_t *addr = zmk_ble_profile_address(req->index);
        if (addr && !bt_addr_le_eq(addr, BT_ADDR_LE_NONE)) {
            for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
                if (bt_addr_le_eq(&profile_names[i].addr, addr)) {
                    bt_addr_le_copy(&profile_names[i].addr, BT_ADDR_LE_NONE);
                    profile_names[i].name[0] = '\0';
                    break;
                }
            }
        }

        int rc = zmk_ble_prof_disconnect(req->index);
        result.success = (rc == 0);
    }

    resp->which_response_type = zmk_ble_management_Response_unpair_profile_tag;
    resp->response_type.unpair_profile = result;
    return 0;
}

/**
 * Handle GetSplitInfoRequest
 */
static int handle_get_split_info_request(const zmk_ble_management_GetSplitInfoRequest *req,
                                         zmk_ble_management_Response *resp) {
    LOG_DBG("GetSplitInfoRequest");

    zmk_ble_management_GetSplitInfoResponse result =
        zmk_ble_management_GetSplitInfoResponse_init_zero;
    zmk_ble_management_SplitInfo *info = &result.info;

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
    info->is_split = true;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    info->is_central = true;
    info->is_peripheral = false;
    // For central, check if peripheral is connected
    // Note: ZMK doesn't expose a direct API for this, so we'll set it to false for now
    info->peripheral_connected = false;
    info->central_bonded = false;
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_PERIPHERAL)
    info->is_central = false;
    info->is_peripheral = true;
    info->peripheral_connected = false;
    info->central_bonded = zmk_split_bt_peripheral_is_bonded();
#endif
#else
    info->is_split = false;
    info->is_central = false;
    info->is_peripheral = false;
    info->peripheral_connected = false;
    info->central_bonded = false;
#endif

    resp->which_response_type = zmk_ble_management_Response_get_split_info_tag;
    resp->response_type.get_split_info = result;
    return 0;
}

/**
 * Handle ForgetSplitBondRequest
 */
static int handle_forget_split_bond_request(const zmk_ble_management_ForgetSplitBondRequest *req,
                                            zmk_ble_management_Response *resp) {
    LOG_DBG("ForgetSplitBondRequest");

    zmk_ble_management_ForgetSplitBondResponse result =
        zmk_ble_management_ForgetSplitBondResponse_init_zero;

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
    // Clear all bonds to reset split connection
    zmk_ble_clear_all_bonds();
    result.success = true;
#else
    LOG_WRN("Split BLE not enabled");
    result.success = false;
#endif

    resp->which_response_type = zmk_ble_management_Response_forget_split_bond_tag;
    resp->response_type.forget_split_bond = result;
    return 0;
}

/**
 * Initialize profile names on boot
 */
static int profile_names_init(void) {
    // Initialize all entries to empty
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        bt_addr_le_copy(&profile_names[i].addr, BT_ADDR_LE_NONE);
        profile_names[i].name[0] = '\0';
    }

    LOG_DBG("Profile names initialized");
    return 0;
}

SYS_INIT(profile_names_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
