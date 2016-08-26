
/*
 * Copyright (c) 2016 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */


/*
 * filename: nas_int_bridge.h
 */


#ifndef NAS_INTF_BRIDGE_H_
#define NAS_INTF_BRIDGE_H_

#include "ds_common_types.h"
#include "std_error_codes.h"
#include "dell-base-vlan.h"
#include "ietf-interfaces.h"
#include "hal_if_mapping.h"
#include <unordered_map>


#ifdef __cplusplus

extern "C" {
#endif

#include "nas_int_list.h"
#define SYSTEM_DEFAULT_VLAN 1
#define MAC_STRING_SZ  18   // string format -> 01:02:03:04:05:06
typedef struct nas_bridge_s{
    hal_ifindex_t ifindex;      //Kernel ifindex of the bridge
    hal_vlan_id_t vlan_id;      //One bridge maps to one Vlan ID in the NPU
    char mac_addr[MAC_STRING_SZ];  //MAC address of this bridge
    char name[HAL_IF_NAME_SZ]; //Just store it for now.
    IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_t admin_status;
    bool learning_disable;     //learning disable state.
    nas_list_t untagged_list; //untagged vlan ports in this bridge
    nas_list_t tagged_list; //tagged vlan ports in this bridge
    nas_list_t untagged_lag; //Untagged LAG index to handle
}nas_bridge_t;

typedef std::unordered_map <hal_ifindex_t, nas_bridge_t> bridge_list_t;
/**
 * @brief Create and insert the bridge details in a RB tree
 *
 * @param index - ifindex of the bridge
 *
 * @return pointer to bridge structure, NULL in case of failure in creation
 */

nas_bridge_t* nas_create_insert_bridge_node(hal_ifindex_t index, const char *name, bool &create);

/**
 * @brief Delete the bridge
 *
 * @param index - ifindex of the bridge
 *
 * @return standard error
 */
t_std_error nas_delete_bridge(hal_ifindex_t index);

/**
 * @brief Finds the bridge data structure
 *
 * @param index - ifindex of the bridge
 *
 * @return bridge node if found or NULL
 */

nas_bridge_t *nas_get_bridge_node(hal_ifindex_t index);

/**
 * @brief Finds the bridge data structure
 *
 * @param vlan_if_name - Name of the bridge
 *
 * @return bridge node if found or NULL
 */

nas_bridge_t *nas_get_bridge_node_from_name(const char *vlan_if_name);

bool nas_find_vlan_if_exist(hal_vlan_id_t vlan_id,  nas_bridge_t *p_current_bridge);
/**
 * @brief Utility to use simple mutex lock for bridge resources access.
 */
void nas_bridge_lock (void);

/**
 * @brief Utility to release simple mutex lock for Bridge resources access.
 */
void nas_bridge_unlock (void);

/**
 * @brief : Cleanup the bridge. Internally delete the vlan from NPU and free the lists etc.
 */

t_std_error nas_cleanup_bridge(nas_bridge_t *p_bridge_node);

#ifdef __cplusplus
}
#endif

#endif /* NAS_INT_BRIDGE_H_ */
