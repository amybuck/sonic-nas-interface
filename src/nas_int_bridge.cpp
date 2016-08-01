
/*
 * Copyright (c) 2016 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 *  LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */


/*
 * filename: nas_int_bridge.c
 */


#include "nas_int_bridge.h"
#include "std_mutex_lock.h"
#include "event_log.h"
#include "event_log_types.h"
#include "hal_interface_common.h"
#include "nas_int_utils.h"
#include "nas_int_vlan.h"
#include "cps_api_object.h"
#include "unordered_map"
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static bridge_list_t bridge_list;

//@TODO gcc define to allow use of recursive mutexes is required.
//then switch to recursive mutex
static std_mutex_lock_create_static_init_fast(br_lock);

bool nas_find_vlan_if_exist(hal_vlan_id_t vlan_id,  nas_bridge_t *p_current_bridge)
{

   for ( auto local_it = bridge_list.begin(); local_it!= bridge_list.end(); ++local_it ) {
       if (p_current_bridge == &local_it->second) {
           //skip the current bridge node
           continue;
       }
       nas_bridge_t &bridge_entry = local_it->second;
       if (bridge_entry.vlan_id == vlan_id) {
           EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                        "Found another bridge %d with vlan-id %d",
                        bridge_entry.ifindex, vlan_id);
            return true;
       }
  }
  return false;

}

t_std_error nas_vlan_get_all_info(cps_api_object_list_t list)
{

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
           "Getting all vlan interfaces");

    for ( auto local_it = bridge_list.begin(); local_it!= bridge_list.end(); ++local_it ) {
        cps_api_object_t obj = cps_api_object_list_create_obj_and_append(list);
        nas_pack_vlan_if(obj, &local_it->second);
    }
    return STD_ERR_OK;
}

void nas_bridge_lock(void)
{
    std_mutex_lock (&br_lock);
}

void nas_bridge_unlock(void)
{
    std_mutex_unlock (&br_lock);
}


nas_bridge_t *nas_get_bridge_node(hal_ifindex_t index)
{

    auto it = bridge_list.find(index);

    if (it == bridge_list.end()) {
       EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MAJOR, "NAS-Br","Error finding bridge intf %d", index);
       return NULL;
    }
    EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Br", "Found Bridge node %d", index);
    return &it->second;
}

nas_bridge_t *nas_get_bridge_node_from_name (const char *vlan_if_name)
{
    if (vlan_if_name == NULL) return NULL;
    hal_ifindex_t if_index;
    if (nas_int_name_to_if_index(&if_index, vlan_if_name) == STD_ERR_OK) {
        return(nas_get_bridge_node(if_index));
    }
    return NULL;
}
nas_bridge_t* nas_create_insert_bridge_node(hal_ifindex_t index, const char *name, bool &create)
{
    nas_bridge_t *p_bridge_node;

    p_bridge_node = nas_get_bridge_node(index);
    create = false;

    if (p_bridge_node == NULL) {
        nas_bridge_t node;
        memset(&node, 0, sizeof(node));
        EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Br",
                    "Bridge intf %d created", index);
        node.ifindex = index;
        strncpy(node.name, name, sizeof(node.name));

        bridge_list[index] = node;
        nas_bridge_t *p_node = &bridge_list.at(index);
        std_dll_init (&p_node->tagged_list.port_list);
        std_dll_init (&p_node->untagged_list.port_list);
        std_dll_init (&p_node->untagged_lag.port_list);
        create = true;
        return p_node;
    } else {
        EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Br",
                    "Bridge intf index %d already exists", index);
    }
    return p_bridge_node;
}

t_std_error nas_cleanup_bridge(nas_bridge_t *p_bridge_node)
{
    t_std_error rc = STD_ERR_OK;
    //If vlan is set for this bridge, delete VLAN from NPU
    if((p_bridge_node->vlan_id != 0) && (p_bridge_node->vlan_id != SYSTEM_DEFAULT_VLAN)) {
        /* @TODO - NPU ID for bridge structure */
        if(nas_vlan_delete(0, p_bridge_node->vlan_id) != STD_ERR_OK) {
            EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Br",
                       "Error deleting vlan %d for bridge %d",
                        p_bridge_node->vlan_id, p_bridge_node->ifindex);
            rc = (STD_ERR(INTERFACE,FAIL, 0));
        }
    }
    nas_delete_port_list(&(p_bridge_node->untagged_list));
    nas_delete_port_list(&(p_bridge_node->tagged_list));
    nas_delete_port_list(&(p_bridge_node->untagged_lag));
    /* Deregister the VLAN intf from ifCntrl - vlanId in open source gets created
     * only after an interface is added in the bridge */
    if(p_bridge_node->vlan_id !=0 ) {
        if(nas_publish_vlan_object(p_bridge_node, cps_api_oper_DELETE)!= cps_api_ret_code_OK)
        {
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                    "Failure publishing VLAN delete event");
        }
        if(nas_register_vlan_intf(p_bridge_node, HAL_INTF_OP_DEREG) != STD_ERR_OK) {
            rc = (STD_ERR(INTERFACE, FAIL, 0));
        }
    }
    /* Delete the bridge */
    bridge_list.erase(p_bridge_node->ifindex);
    return rc;
}
t_std_error nas_delete_bridge(hal_ifindex_t index)
{
    t_std_error rc = STD_ERR_OK;

    EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Br",
                "Bridge intf %d for deletion", index);

    nas_bridge_lock();
    do {
        nas_bridge_t *p_bridge_node = nas_get_bridge_node(index);
        if (p_bridge_node==NULL) {
            EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Br",
                       "Error finding bridge %d for deletion", index);
            rc = (STD_ERR(INTERFACE, FAIL, 0));
            break;
        }

        if(nas_cleanup_bridge(p_bridge_node) != STD_ERR_OK) {
            rc = (STD_ERR(INTERFACE, FAIL, 0));
        }

    }while (0);

    nas_bridge_unlock();
    return rc;
}
