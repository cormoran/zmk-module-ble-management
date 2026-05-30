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
 * - Set and get output priority (USB or BLE)
 */

#include <pb_decode.h>
#include <pb_encode.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/init.h>
#include <zephyr/sys/iterable_sections.h>
#include <zmk/ble.h>
#include <zmk/ble_management/ble_management.pb.h>
#include <zmk/custom_settings.h>
#include <zmk/endpoints.h>
#include <zmk/studio/custom.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_PERIPHERAL)
#include <zmk/split/bluetooth/peripheral.h>
#endif
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Structure to store profile name tied to BLE address
struct profile_name_entry {
#if IS_ENABLED(CONFIG_ZMK_BLE)
    bt_addr_le_t addr;
#endif
    char name[32];
};

#if IS_ENABLED(CONFIG_ZMK_BLE)
BUILD_ASSERT(sizeof(struct profile_name_entry) <= CONFIG_ZMK_CUSTOM_SETTINGS_VALUE_MAX_SIZE,
             "profile_name_entry too large for configured custom settings value size");

// Register one BYTES custom setting per profile name slot (slots 0-7).
// ZMK limits BLE profiles to 8 (CONFIG_ZMK_BLE_PROFILES_COUNT range 1 8), so 8
// slots cover all supported configurations. ZMK_BLE_PROFILE_COUNT determines
// how many slots are accessed at runtime.
#define BLE_MGMT_PROFILE_NAME_SETTING(_sym, _idx)                                           \
    static const struct zmk_custom_setting_constraint _sym##_constraints[] = {              \
        {.type = ZMK_CUSTOM_SETTING_CONSTRAINT_NONE}};                                      \
    STRUCT_SECTION_ITERABLE(zmk_custom_setting, _sym) = {                                   \
        .custom_subsystem_id = "cormoran_ble",                                              \
        .key = "pname/" ZMK_CUSTOM_SETTINGS_STRINGIFY(_idx),                                \
        .array_index = ZMK_CUSTOM_SETTING_ARRAY_NONE,                                       \
        .value_type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES,                                  \
        .confidentiality = ZMK_CUSTOM_SETTING_CONFIDENTIALITY_DEVICE_PRIVATE,               \
        .read_permission = ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,                          \
        .write_permission = ZMK_CUSTOM_SETTING_PERMISSION_UNSECURE,                         \
        .constraints = _sym##_constraints,                                                  \
        .constraints_count = ARRAY_SIZE(_sym##_constraints),                                \
        .default_value = {.type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES,                      \
                          .size = sizeof(struct profile_name_entry),                        \
                          .bytes_value = {0}},                                              \
    }

BLE_MGMT_PROFILE_NAME_SETTING(ble_mgmt_pname_0, 0);
BLE_MGMT_PROFILE_NAME_SETTING(ble_mgmt_pname_1, 1);
BLE_MGMT_PROFILE_NAME_SETTING(ble_mgmt_pname_2, 2);
BLE_MGMT_PROFILE_NAME_SETTING(ble_mgmt_pname_3, 3);
BLE_MGMT_PROFILE_NAME_SETTING(ble_mgmt_pname_4, 4);
BLE_MGMT_PROFILE_NAME_SETTING(ble_mgmt_pname_5, 5);
BLE_MGMT_PROFILE_NAME_SETTING(ble_mgmt_pname_6, 6);
BLE_MGMT_PROFILE_NAME_SETTING(ble_mgmt_pname_7, 7);

static const struct zmk_custom_setting *const profile_name_settings[] = {
    &ble_mgmt_pname_0, &ble_mgmt_pname_1, &ble_mgmt_pname_2, &ble_mgmt_pname_3,
    &ble_mgmt_pname_4, &ble_mgmt_pname_5, &ble_mgmt_pname_6, &ble_mgmt_pname_7,
};

BUILD_ASSERT(ZMK_BLE_PROFILE_COUNT <= (int)ARRAY_SIZE(profile_name_settings),
             "ZMK_BLE_PROFILE_COUNT exceeds the 8-slot maximum supported by ble_management");
#endif

/**
 * Metadata for the custom subsystem.
 */
static struct zmk_rpc_custom_subsystem_meta ble_management_meta = {
    ZMK_RPC_CUSTOM_SUBSYSTEM_UI_URLS(
        "https://cormoran.github.io/zmk-module-ble-management/"),
    .security = ZMK_STUDIO_RPC_HANDLER_UNSECURED,
};

/**
 * Register the custom RPC subsystem.
 */
ZMK_RPC_CUSTOM_SUBSYSTEM(cormoran_ble, &ble_management_meta,
                         ble_management_rpc_handle_request);

ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER(cormoran_ble,
                                         zmk_ble_management_Response);

// Forward declarations
static int handle_get_profiles_request(
    const zmk_ble_management_GetProfilesRequest *req,
    zmk_ble_management_Response *resp);
static int handle_set_profile_name_request(
    const zmk_ble_management_SetProfileNameRequest *req,
    zmk_ble_management_Response *resp);
static int handle_switch_profile_request(
    const zmk_ble_management_SwitchProfileRequest *req,
    zmk_ble_management_Response *resp);
static int handle_unpair_profile_request(
    const zmk_ble_management_UnpairProfileRequest *req,
    zmk_ble_management_Response *resp);
static int handle_get_split_info_request(
    const zmk_ble_management_GetSplitInfoRequest *req,
    zmk_ble_management_Response *resp);
static int handle_forget_split_bond_request(
    const zmk_ble_management_ForgetSplitBondRequest *req,
    zmk_ble_management_Response *resp);
static int handle_set_output_priority_request(
    const zmk_ble_management_SetOutputPriorityRequest *req,
    zmk_ble_management_Response *resp);
static int handle_get_output_priority_request(
    const zmk_ble_management_GetOutputPriorityRequest *req,
    zmk_ble_management_Response *resp);

/**
 * Get profile name from custom settings based on BLE address.
 * Copies the name into out_name (caller-provided buffer).
 */
static void get_profile_name(const bt_addr_le_t *addr, char *out_name, size_t name_size) {
    if (!out_name || name_size == 0) {
        return;
    }
    out_name[0] = '\0';
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (!addr) {
        return;
    }

    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        struct zmk_custom_setting_value val;
        if (zmk_custom_setting_read(profile_name_settings[i], &val) != 0) {
            continue;
        }
        if (val.size < sizeof(struct profile_name_entry)) {
            continue;
        }

        struct profile_name_entry entry;
        memcpy(&entry, val.bytes_value, sizeof(entry));
        if (bt_addr_le_eq(&entry.addr, addr)) {
            strncpy(out_name, entry.name, name_size - 1);
            out_name[name_size - 1] = '\0';
            return;
        }
    }
#endif
}

/**
 * Save profile name to custom settings indexed by BLE address.
 * Finds the slot already holding addr, or the first empty slot.
 */
static int save_profile_name(const bt_addr_le_t *addr, const char *name) {
#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (!addr || !name) {
        return -EINVAL;
    }

    int empty_slot = -1;
    for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        struct zmk_custom_setting_value val;
        struct profile_name_entry entry = {0};
        if (zmk_custom_setting_read(profile_name_settings[i], &val) == 0 &&
            val.size >= sizeof(entry)) {
            memcpy(&entry, val.bytes_value, sizeof(entry));
        }

        if (bt_addr_le_eq(&entry.addr, addr)) {
            strncpy(entry.name, name, sizeof(entry.name) - 1);
            entry.name[sizeof(entry.name) - 1] = '\0';

            struct zmk_custom_setting_value new_val = {
                .type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES,
                .size = sizeof(entry),
            };
            memcpy(new_val.bytes_value, &entry, sizeof(entry));
            return zmk_custom_setting_write(profile_name_settings[i], &new_val,
                                            ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST);
        }

        if (empty_slot == -1 && bt_addr_le_eq(&entry.addr, BT_ADDR_LE_NONE)) {
            empty_slot = i;
        }
    }

    if (empty_slot == -1) {
        LOG_WRN("No slot available for profile name");
        return -ENOMEM;
    }

    struct profile_name_entry new_entry;
    bt_addr_le_copy(&new_entry.addr, addr);
    strncpy(new_entry.name, name, sizeof(new_entry.name) - 1);
    new_entry.name[sizeof(new_entry.name) - 1] = '\0';

    struct zmk_custom_setting_value new_val = {
        .type = ZMK_CUSTOM_SETTING_VALUE_TYPE_BYTES,
        .size = sizeof(new_entry),
    };
    memcpy(new_val.bytes_value, &new_entry, sizeof(new_entry));
    return zmk_custom_setting_write(profile_name_settings[empty_slot], &new_val,
                                    ZMK_CUSTOM_SETTING_WRITE_MODE_PERSIST);
#else
    return -ENOTSUP;
#endif
}

/**
 * Main request handler for the custom RPC subsystem.
 */
static bool ble_management_rpc_handle_request(
    const zmk_custom_CallRequest *raw_request, pb_callback_t *encode_response) {
    zmk_ble_management_Response *resp =
        ZMK_RPC_CUSTOM_SUBSYSTEM_RESPONSE_BUFFER_ALLOCATE(cormoran_ble,
                                                          encode_response);

    zmk_ble_management_Request req = zmk_ble_management_Request_init_zero;

    // Decode the incoming request
    pb_istream_t req_stream = pb_istream_from_buffer(raw_request->payload.bytes,
                                                     raw_request->payload.size);
    if (!pb_decode(&req_stream, zmk_ble_management_Request_fields, &req)) {
        LOG_WRN("Failed to decode ble_management request: %s",
                PB_GET_ERROR(&req_stream));
        zmk_ble_management_ErrorResponse err =
            zmk_ble_management_ErrorResponse_init_zero;
        snprintf(err.message, sizeof(err.message), "Failed to decode request");
        resp->which_response_type = zmk_ble_management_Response_error_tag;
        resp->response_type.error = err;
        return true;
    }

    int rc = 0;
    switch (req.which_request_type) {
        case zmk_ble_management_Request_get_profiles_tag:
            rc = handle_get_profiles_request(&req.request_type.get_profiles,
                                             resp);
            break;
        case zmk_ble_management_Request_set_profile_name_tag:
            rc = handle_set_profile_name_request(
                &req.request_type.set_profile_name, resp);
            break;
        case zmk_ble_management_Request_switch_profile_tag:
            rc = handle_switch_profile_request(&req.request_type.switch_profile,
                                               resp);
            break;
        case zmk_ble_management_Request_unpair_profile_tag:
            rc = handle_unpair_profile_request(&req.request_type.unpair_profile,
                                               resp);
            break;
        case zmk_ble_management_Request_get_split_info_tag:
            rc = handle_get_split_info_request(&req.request_type.get_split_info,
                                               resp);
            break;
        case zmk_ble_management_Request_forget_split_bond_tag:
            rc = handle_forget_split_bond_request(
                &req.request_type.forget_split_bond, resp);
            break;
        case zmk_ble_management_Request_set_output_priority_tag:
            rc = handle_set_output_priority_request(
                &req.request_type.set_output_priority, resp);
            break;
        case zmk_ble_management_Request_get_output_priority_tag:
            rc = handle_get_output_priority_request(
                &req.request_type.get_output_priority, resp);
            break;
        default:
            LOG_WRN("Unsupported request type: %d", req.which_request_type);
            rc = -ENOTSUP;
    }

    if (rc != 0) {
        zmk_ble_management_ErrorResponse err =
            zmk_ble_management_ErrorResponse_init_zero;
        snprintf(err.message, sizeof(err.message),
                 "Failed to process request: %d", rc);
        resp->which_response_type = zmk_ble_management_Response_error_tag;
        resp->response_type.error = err;
    }
    return true;
}

/**
 * Handle GetProfilesRequest
 */
static int handle_get_profiles_request(
    const zmk_ble_management_GetProfilesRequest *req,
    zmk_ble_management_Response *resp) {
    LOG_DBG("GetProfilesRequest");

    zmk_ble_management_GetProfilesResponse result =
        zmk_ble_management_GetProfilesResponse_init_zero;

#if IS_ENABLED(CONFIG_ZMK_BLE)
    result.max_profiles = ZMK_BLE_PROFILE_COUNT;

    int active = zmk_ble_active_profile_index();

    for (uint8_t i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
        zmk_ble_management_ProfileInfo *profile = &result.profiles[i];
        profile->index                          = i;
        profile->is_open                        = zmk_ble_profile_is_open(i);
        profile->is_connected = zmk_ble_profile_is_connected(i);
        profile->is_active    = (i == active);

        // Get BLE address
        bt_addr_le_t *addr = zmk_ble_profile_address(i);
        if (addr && !bt_addr_le_eq(addr, BT_ADDR_LE_NONE)) {
            char addr_str[BT_ADDR_LE_STR_LEN];
            bt_addr_le_to_str(addr, addr_str, sizeof(addr_str));
            strncpy(profile->address, addr_str, sizeof(profile->address) - 1);
            profile->address[sizeof(profile->address) - 1] = '\0';

            // Get custom name
            char name_buf[32];
            get_profile_name(addr, name_buf, sizeof(name_buf));
            if (name_buf[0] != '\0') {
                strncpy(profile->name, name_buf, sizeof(profile->name) - 1);
                profile->name[sizeof(profile->name) - 1] = '\0';
            }
        }
    }

    result.profiles_count = ZMK_BLE_PROFILE_COUNT;
#else
    result.max_profiles   = 0;
    result.profiles_count = 0;
#endif

    resp->which_response_type = zmk_ble_management_Response_get_profiles_tag;
    resp->response_type.get_profiles = result;
    return 0;
}

/**
 * Handle SetProfileNameRequest
 */
static int handle_set_profile_name_request(
    const zmk_ble_management_SetProfileNameRequest *req,
    zmk_ble_management_Response *resp) {
    LOG_DBG("SetProfileNameRequest: index=%d, name=%s", req->index, req->name);

    zmk_ble_management_SetProfileNameResponse result =
        zmk_ble_management_SetProfileNameResponse_init_zero;

#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (req->index >= ZMK_BLE_PROFILE_COUNT) {
        LOG_WRN("Invalid profile index: %d", req->index);
        result.success = false;
    } else {
        bt_addr_le_t *addr = zmk_ble_profile_address(req->index);
        if (addr && !bt_addr_le_eq(addr, BT_ADDR_LE_NONE)) {
            int rc         = save_profile_name(addr, req->name);
            result.success = (rc == 0);
        } else {
            LOG_WRN("Profile %d has no address", req->index);
            result.success = false;
        }
    }
#else
    result.success = false;
#endif

    resp->which_response_type =
        zmk_ble_management_Response_set_profile_name_tag;
    resp->response_type.set_profile_name = result;
    return 0;
}

/**
 * Handle SwitchProfileRequest
 */
static int handle_switch_profile_request(
    const zmk_ble_management_SwitchProfileRequest *req,
    zmk_ble_management_Response *resp) {
    LOG_DBG("SwitchProfileRequest: index=%d", req->index);

    zmk_ble_management_SwitchProfileResponse result =
        zmk_ble_management_SwitchProfileResponse_init_zero;

#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (req->index >= ZMK_BLE_PROFILE_COUNT) {
        LOG_WRN("Invalid profile index: %d", req->index);
        result.success = false;
    } else {
        int rc         = zmk_ble_prof_select(req->index);
        result.success = (rc == 0);
    }
#else
    result.success = false;
#endif

    resp->which_response_type = zmk_ble_management_Response_switch_profile_tag;
    resp->response_type.switch_profile = result;
    return 0;
}

/**
 * Handle UnpairProfileRequest
 */
static int handle_unpair_profile_request(
    const zmk_ble_management_UnpairProfileRequest *req,
    zmk_ble_management_Response *resp) {
    LOG_DBG("UnpairProfileRequest: index=%d", req->index);

    zmk_ble_management_UnpairProfileResponse result =
        zmk_ble_management_UnpairProfileResponse_init_zero;

#if IS_ENABLED(CONFIG_ZMK_BLE)
    if (req->index >= ZMK_BLE_PROFILE_COUNT) {
        LOG_WRN("Invalid profile index: %d", req->index);
        result.success = false;
    } else {
        // Clear profile name from settings if the profile has an address
        bt_addr_le_t *addr = zmk_ble_profile_address(req->index);
        if (addr && !bt_addr_le_eq(addr, BT_ADDR_LE_NONE)) {
            for (int i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
                struct zmk_custom_setting_value val;
                struct profile_name_entry entry = {0};
                if (zmk_custom_setting_read(profile_name_settings[i], &val) == 0 &&
                    val.size >= sizeof(entry)) {
                    memcpy(&entry, val.bytes_value, sizeof(entry));
                }
                if (bt_addr_le_eq(&entry.addr, addr)) {
                    zmk_custom_setting_reset(profile_name_settings[i]);
                    break;
                }
            }
        }
        int active = zmk_ble_active_profile_index();
        int rc     = zmk_ble_prof_select(req->index);
        if (rc == 0) {
            zmk_ble_clear_bonds();
            if (active != req->index) {
                rc = zmk_ble_prof_select(active);
            }
        }
        result.success = (rc == 0);
    }
#else
    result.success = false;
#endif

    resp->which_response_type = zmk_ble_management_Response_unpair_profile_tag;
    resp->response_type.unpair_profile = result;
    return 0;
}

/**
 * Handle GetSplitInfoRequest
 */
static int handle_get_split_info_request(
    const zmk_ble_management_GetSplitInfoRequest *req,
    zmk_ble_management_Response *resp) {
    LOG_DBG("GetSplitInfoRequest");

    zmk_ble_management_GetSplitInfoResponse result =
        zmk_ble_management_GetSplitInfoResponse_init_zero;
    zmk_ble_management_SplitInfo *info = &result.info;

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE)
    info->is_split = true;
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    info->is_central = true;
    // For central, check if peripheral is connected
    // Note: ZMK doesn't expose a direct API for this, so we'll set it to false
    // for now
    info->peripheral_connected = false;
    info->central_bonded       = false;
#elif IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_PERIPHERAL)
    info->is_central           = false;
    info->peripheral_connected = false;
    info->central_bonded       = zmk_split_bt_peripheral_is_bonded();
#endif
#else
    info->is_split             = false;
    info->is_central           = false;
    info->peripheral_connected = false;
    info->central_bonded       = false;
#endif

    resp->which_response_type = zmk_ble_management_Response_get_split_info_tag;
    resp->response_type.get_split_info = result;
    return 0;
}

/**
 * Handle ForgetSplitBondRequest
 */
static int handle_forget_split_bond_request(
    const zmk_ble_management_ForgetSplitBondRequest *req,
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

    resp->which_response_type =
        zmk_ble_management_Response_forget_split_bond_tag;
    resp->response_type.forget_split_bond = result;
    return 0;
}

/**
 * Handle SetOutputPriorityRequest
 */
static int handle_set_output_priority_request(
    const zmk_ble_management_SetOutputPriorityRequest *req,
    zmk_ble_management_Response *resp) {
    LOG_DBG("SetOutputPriorityRequest: priority=%d", req->priority);

    zmk_ble_management_SetOutputPriorityResponse result =
        zmk_ble_management_SetOutputPriorityResponse_init_zero;

    // Convert protobuf enum to ZMK transport enum
    enum zmk_transport transport;
    switch (req->priority) {
        case zmk_ble_management_OutputPriority_OUTPUT_PRIORITY_USB:
            transport = ZMK_TRANSPORT_USB;
            break;
        case zmk_ble_management_OutputPriority_OUTPUT_PRIORITY_BLE:
            transport = ZMK_TRANSPORT_BLE;
            break;
        default:
            LOG_WRN("Invalid output priority: %d", req->priority);
            result.success = false;
            resp->which_response_type =
                zmk_ble_management_Response_set_output_priority_tag;
            resp->response_type.set_output_priority = result;
            return 0;
    }

    int rc         = zmk_endpoint_set_preferred_transport(transport);
    result.success = (rc == 0);

    resp->which_response_type =
        zmk_ble_management_Response_set_output_priority_tag;
    resp->response_type.set_output_priority = result;
    return 0;
}

/**
 * Handle GetOutputPriorityRequest
 */
static int handle_get_output_priority_request(
    const zmk_ble_management_GetOutputPriorityRequest *req,
    zmk_ble_management_Response *resp) {
    LOG_DBG("GetOutputPriorityRequest");

    zmk_ble_management_GetOutputPriorityResponse result =
        zmk_ble_management_GetOutputPriorityResponse_init_zero;

    // Get the currently selected endpoint
    enum zmk_transport current = zmk_endpoint_get_preferred_transport();

    // Convert ZMK transport enum to protobuf enum
    switch (current) {
        case ZMK_TRANSPORT_USB:
            result.priority =
                zmk_ble_management_OutputPriority_OUTPUT_PRIORITY_USB;
            break;
        case ZMK_TRANSPORT_BLE:
            result.priority =
                zmk_ble_management_OutputPriority_OUTPUT_PRIORITY_BLE;
            break;
        default:
            LOG_WRN("Unknown transport type: %d", current);
            result.priority =
                zmk_ble_management_OutputPriority_OUTPUT_PRIORITY_BLE;
            break;
    }

    resp->which_response_type =
        zmk_ble_management_Response_get_output_priority_tag;
    resp->response_type.get_output_priority = result;
    return 0;
}
