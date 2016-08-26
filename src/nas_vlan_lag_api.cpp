
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
 * nas_vlan_lag_api.cpp
 *
 */

#include "std_error_codes.h"
#include "cps_api_object_attr.h"
#include "cps_api_object_key.h"
#include "cps_api_object.h"
#include "event_log.h"
#include "event_log_types.h"
#include "nas_vlan_lag.h"
#include "nas_int_vlan.h"
#include "nas_int_utils.h"
#include "hal_interface_defaults.h"
#include "dell-base-if.h"
#include "dell-interface.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>


typedef std::unordered_set <hal_ifindex_t> nas_vlan_untag_lag_list_t;
typedef std::unordered_set <hal_ifindex_t> nas_vlan_tag_lag_list_t;

typedef std::unordered_map <hal_ifindex_t, nas_vlan_lag_t> nas_lag_list_t;
typedef std::unordered_map <hal_ifindex_t, nas_vlan_lag_t >::iterator nas_lag_list_it;

static nas_lag_list_t lag_list;

static t_std_error nas_vlan_create_lag(hal_ifindex_t ifindex)
{
    nas_lag_list_it lag_it = lag_list.find(ifindex);

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-VLAN-LAG",
            "Add LAG %d using CPS", ifindex);

        /* Now that LAG exists, proceed further */
    if(lag_it == lag_list.end()) {
        nas_vlan_lag_t vlan_lag_entry;

        vlan_lag_entry.lag_index = ifindex;
        lag_list[ifindex] = vlan_lag_entry;
    }
    else {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-VLAN-LAG",
                " Lag IfIndex %d already created", ifindex);
        return (STD_ERR(INTERFACE,FAIL, 0));
    }
    return STD_ERR_OK;
}

static t_std_error nas_vlan_delete_lag(hal_ifindex_t ifindex) {
    nas_lag_list_it lag_it = lag_list.find(ifindex);

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "Delete LAG %d using CPS", ifindex);

    if(lag_it == lag_list.end()) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "Missing Lag IfIndex %d ", ifindex);
        return (STD_ERR(INTERFACE,FAIL, 0));
    }

    lag_list.erase(ifindex);

    return STD_ERR_OK;
}

/**
 * This function to add/del vlan entries when a new member gets added
 * or deleted in a LAG group. Multiple Vlans could be configured on a LAG
 * interface - walk through all the vlans and invokes SAI calls.
 *
 */
static t_std_error nas_add_or_del_vlan_on_lag_mem_update(nas_vlan_lag_t *lag_entry,
                                                  ndi_port_t *ndi_port,
                                                  bool add_port)
{
    nas_vlan_list_it vlan_it;

    for(vlan_it = lag_entry->tagged_list.begin();
        vlan_it != lag_entry->tagged_list.end(); ++vlan_it) {
        if(nas_add_or_del_port_to_vlan(0, *vlan_it,
                                       ndi_port, NAS_PORT_TAGGED, add_port) != STD_ERR_OK) {
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-VLAN",
                    "Error in LAG mem %d %d update for Vlan %d",
                    ndi_port->npu_id, ndi_port->npu_port, *vlan_it);

        }
    }

    for(vlan_it = lag_entry->untagged_list.begin();
        vlan_it != lag_entry->untagged_list.end(); ++vlan_it) {
        if(nas_add_or_del_port_to_vlan(0, *vlan_it,
                                       ndi_port, NAS_PORT_UNTAGGED, add_port) != STD_ERR_OK) {
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-VLAN",
                    "Error in LAG mem %d %d update for Vlan %d",
                    ndi_port->npu_id, ndi_port->npu_port, *vlan_it);

        }
    }
    return STD_ERR_OK;
}

static t_std_error nas_process_lag_mem_updates_for_vlan(hal_ifindex_t ifindex, nas_port_list_t &port_list) {

    nas_port_list_it port_it;
    nas_lag_mem_list_it it;
    ndi_port_t ndi_port;
    t_std_error rc = STD_ERR_OK;
    nas_vlan_lag_t *lag_entry;

    /* First check all ports from the lag_mem in the incoming list,
     * if anything is missing in incomign list then it has been deleted */
    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
           "LAG %d recvd member update !!!!", ifindex);

    nas_lag_list_it lag_it = lag_list.find(ifindex);

    if(lag_it == lag_list.end()) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
               "Invalid LAG % in VLAN !", ifindex);
        return (STD_ERR(INTERFACE,FAIL, 0));
    }

    lag_entry = &(lag_it->second);

    for(it = lag_entry->lag_mem.begin(); it != lag_entry->lag_mem.end();) {

        nas_port_node_t &port_node = it->second;
        port_it = port_list.find(port_node.ifindex);

        if(port_it == port_list.end()) {
            // Port is missing, delete it
            EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                   "Port %d deleted from LAG % ", port_node.ifindex, ifindex);

            if(lag_entry->vlan_enable) {
                nas_add_or_del_vlan_on_lag_mem_update(lag_entry, &(port_node.ndi_port), false);
            }
            //Now erase the member from lag_entry
            lag_entry->lag_mem.erase(it);
            it = lag_entry->lag_mem.begin();
        }
        else {
            ++it;
        }
    }

    /* Now lets do the addition of new ports */

    for (port_it = port_list.begin() ; port_it != port_list.end() ; ++port_it) {
        EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
               "Checking for Port %d presence in LAG %d", *port_it, ifindex);

        it = lag_entry->lag_mem.find(*port_it);
        /* This member is not present in the lag db, add it */
        if(it == lag_entry->lag_mem.end()) {
            EV_LOG(INFO, INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                   "New Port %d added in LAG % ", *port_it, ifindex);

            nas_port_node_t lag_mem;
            lag_mem.ifindex = *port_it;

            if((rc = (nas_int_get_npu_port(lag_mem.ifindex, &ndi_port))) != STD_ERR_OK) {
                EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                           "Interface %d returned error %d ",
                            lag_mem.ifindex, rc);
                return rc;
            }

            memcpy(&(lag_mem.ndi_port), &ndi_port, sizeof(ndi_port));

            lag_entry->lag_mem[*port_it] = lag_mem;
            //New port in LAG group, add it to NPU
            if(lag_entry->vlan_enable) {
                nas_add_or_del_vlan_on_lag_mem_update(lag_entry, &(lag_mem.ndi_port), true);
            }
            else {
                EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                           "Interface %d not enabled for Vlan",
                            ifindex);
            }
        }
    }
    return STD_ERR_OK;
}

static t_std_error nas_vlan_cps_set_lag(hal_ifindex_t ifindex, cps_api_object_t obj)
{
    cps_api_object_it_t it;
    nas_port_list_t port_list;
    bool port_list_attr = false;

    EV_LOG(INFO, INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
           "Received set for LAG %d", ifindex);

    cps_api_object_it_begin(obj,&it);

    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        int id = (int) cps_api_object_attr_id(it.attr);
        EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                       "Received attrib %d for LAG %d", id, ifindex);


        switch (id) {
            case DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS:
                port_list_attr = true;
                if (cps_api_object_attr_len(it.attr) != 0) {
                    port_list.insert(cps_api_object_attr_data_u32(it.attr));
                }
                break;
            default :
                EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                       "Attribute %d for LAG", id);
                break;
        }
    }
    if(port_list_attr == true) {
        nas_process_lag_mem_updates_for_vlan(ifindex, port_list);
    }
    return STD_ERR_OK;
}

bool nas_vlan_lag_event_func_cb(cps_api_object_t obj, void *param) {
    cps_api_object_attr_t lag_attr = cps_api_get_key_data(obj, DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX);

    EV_LOG(ERR, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
           "Callback for LAG notification!!!");

    if (lag_attr == NULL) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
               "Missing ifindex attribute for CPS LAG CB");
        return false;
    }

    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));
    hal_ifindex_t ifindex = (hal_ifindex_t)cps_api_object_attr_data_u32(lag_attr);

    if(op == cps_api_oper_CREATE) {
        nas_vlan_create_lag(ifindex);
    }
    else if (op == cps_api_oper_DELETE) {
        nas_vlan_delete_lag(ifindex);
    }
    else if (op == cps_api_oper_SET) {
        nas_vlan_cps_set_lag(ifindex, obj);
    }
    return true;
}

/* Function to add or delete all the LAG members in SAI
 * */
static t_std_error nas_add_or_del_lag_in_vlan(nas_vlan_lag_t *lag_entry, hal_vlan_id_t vlan_id,
                                              nas_port_mode_t port_mode, bool add_flag)
{
    nas_lag_mem_list_it it;

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-VLAN",
            "LAG entry %d being updated %d in Vlan %d",
            lag_entry->lag_index, add_flag, vlan_id);

    /* Walk through all the members and add/del ports in VLAN*/
    for(it = lag_entry->lag_mem.begin(); it != lag_entry->lag_mem.end(); ++it) {

        nas_port_node_t &port_node = it->second;

        if(nas_add_or_del_port_to_vlan(0, vlan_id, &port_node.ndi_port,
                                       port_mode, add_flag) != STD_ERR_OK) {
            EV_LOG(ERR, INTERFACE, ev_log_s_MINOR, "NAS-VLAN",
                    "Failure updating port %d LAG entry %d in Vlan %d",
                    port_node.ifindex, lag_entry->lag_index, vlan_id);
        }
    }
    return STD_ERR_OK;

}

/*  Function to add/delete LAG interface to kernel and in the bridge
 *  Once kernel succesful, add all the members in SAI
 *  Create LAG to vlan mapping for traversal when new member gets added or deleted
 * */
t_std_error nas_handle_lag_update_for_vlan(nas_bridge_t *p_bridge, hal_ifindex_t lag_index,
                                           hal_vlan_id_t vlan_id, nas_port_mode_t port_mode,
                                           bool add_flag, bool cps_add) {

    nas_vlan_lag_t *lag_entry;
    nas_lag_mem_list_it it;

    cps_api_operation_types_t op = add_flag ? cps_api_oper_CREATE : cps_api_oper_DELETE;

    nas_lag_list_it lag_it = lag_list.find(lag_index);
    if(lag_it == lag_list.end()) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
               "Invalid LAG %d being added to Vlan %d", lag_index, vlan_id);
        return (STD_ERR(INTERFACE,FAIL, 0));
    }

    lag_entry = &(lag_it->second);

    /* Now that LAG exists, add interface to kernel */
    if(add_flag) {
        nas_vlan_list_it vlan_it;
        if(port_mode == NAS_PORT_TAGGED) {
            vlan_it = lag_entry->tagged_list.find(vlan_id);
            if(vlan_it != lag_entry->tagged_list.end()) {
                EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                       "LAG %d already added to Vlan %d", lag_index, vlan_id);
                return STD_ERR_OK;
            }
        }else {
            vlan_it = lag_entry->untagged_list.find(vlan_id);
            if(vlan_it != lag_entry->untagged_list.end()) {
                EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                       "LAG %d already added to Vlan %d", lag_index, vlan_id);
                return STD_ERR_OK;
            }
        }

        EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
               "Adding LAG %d to bridge %d in OS", lag_index, p_bridge->ifindex);

        if(cps_add == true) {
            if(nas_cps_add_port_to_os(p_bridge->ifindex, vlan_id, port_mode, lag_index) !=
                    STD_ERR_OK) {
                EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                       "Error adding port %d to bridge %d in OS", lag_index, p_bridge->ifindex);
                return STD_ERR(INTERFACE,FAIL, 0);
            }
        }

        nas_add_or_del_lag_in_vlan(lag_entry, vlan_id,
                                   port_mode, add_flag);
        /* Add the vlan id to LAG - need it to traverse when member add/del to
         * LAG happens*/
        if(port_mode == NAS_PORT_TAGGED) {
            lag_entry->tagged_list.insert(vlan_id);
        }
        else {
            lag_entry->untagged_list.insert(vlan_id);
        }
        lag_entry->vlan_enable = true;
        nas_port_list_t publish_list = {lag_entry->lag_index};
        nas_publish_vlan_port_list(p_bridge, publish_list, port_mode, op);
    }
    else {
        EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
               "LAG %d being deleted from Vlan %d", lag_index, vlan_id);

        nas_add_or_del_lag_in_vlan(lag_entry, vlan_id,
                                   port_mode, add_flag);

        /* Delete from Kernel now */
        if(cps_add == true) {

            if(nas_cps_del_port_from_os(vlan_id, lag_index, port_mode) !=
                                STD_ERR_OK) {
                EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                       "Error adding port %d to bridge %d in OS", lag_index, p_bridge->ifindex);
                return STD_ERR(INTERFACE,FAIL, 0);
            }
        }

        if(port_mode == NAS_PORT_TAGGED) {
            lag_entry->tagged_list.erase(vlan_id);
        }
        else {
            lag_entry->untagged_list.erase(vlan_id);
        }
        if (lag_entry->tagged_list.empty() && lag_entry->untagged_list.empty()) {
            lag_entry->vlan_enable = false;
        }
        nas_port_list_t publish_list = {lag_entry->lag_index};
        nas_publish_vlan_port_list(p_bridge, publish_list, port_mode, op);
    }
    return STD_ERR_OK;
}

/**
 * This function walk through all the LAG groups and checks if the incoming vlan list
 * has removed the LAG, then need to clean up from SAI.
 *
 */
t_std_error nas_handle_lag_index_in_cps_set(nas_bridge_t *p_bridge, nas_port_list_t &port_index_list,
                                            nas_port_mode_t port_mode)
{
    for(auto lag_it = lag_list.begin(); lag_it != lag_list.end(); ++lag_it) {
        nas_vlan_lag_t *lag_entry = &(lag_it->second);
        //check in the list only if VLAN is enabled for this LAG
        nas_vlan_list_it vlan_it;
        if(port_mode == NAS_PORT_TAGGED) {
            vlan_it = lag_entry->tagged_list.find(p_bridge->vlan_id);
            if(vlan_it == lag_entry->tagged_list.end())
                continue;
        }else {
            vlan_it = lag_entry->untagged_list.find(p_bridge->vlan_id);
            if(vlan_it == lag_entry->untagged_list.end())
                            continue;
        }

        if((lag_entry->vlan_enable == true) &&
           (port_index_list.find(lag_entry->lag_index) == port_index_list.end())){
            EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                   "LAG index %d not found in incoming list, delete from Vlan %d", lag_entry->lag_index, p_bridge->vlan_id);

            nas_handle_lag_update_for_vlan(p_bridge, lag_entry->lag_index,
                                           p_bridge->vlan_id, port_mode, false, true);
        }
    }


    return STD_ERR_OK;
}


t_std_error nas_base_handle_lag_del(hal_ifindex_t br_index, hal_ifindex_t lag_index,
                                    hal_vlan_id_t vlan_id )
{
    nas_vlan_lag_t *lag_entry;
    nas_lag_mem_list_it it;
    nas_lag_list_it lag_it = lag_list.find(lag_index);
    if(lag_it == lag_list.end()) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
               "Invalid LAG %d being deleted from Vlan %d", lag_index, vlan_id);
        return (STD_ERR(INTERFACE,FAIL, 0));
    }

    lag_entry = &(lag_it->second);

    nas_vlan_list_it vlan_it;
    nas_port_mode_t port_mode;
    vlan_it = lag_entry->tagged_list.find(vlan_id);
    if(vlan_it != lag_entry->tagged_list.end()) {
        port_mode = NAS_PORT_TAGGED;
    }
    else {
        vlan_it = lag_entry->untagged_list.find(vlan_id);

        if(vlan_it != lag_entry->untagged_list.end()) {
            port_mode = NAS_PORT_UNTAGGED;
        }
        else {
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                   "Error finding LAG %d in VLAN lists", lag_index);
            return STD_ERR(INTERFACE,FAIL, 0);
        }
    }

    nas_add_or_del_lag_in_vlan(lag_entry, vlan_id,
                               port_mode, false);

    if(port_mode == NAS_PORT_TAGGED) {
        lag_entry->tagged_list.erase(vlan_id);
    }
    else {
        lag_entry->untagged_list.erase(vlan_id);
    }
    if (lag_entry->tagged_list.empty() && lag_entry->untagged_list.empty()) {
        lag_entry->vlan_enable = false;
    }
    return STD_ERR_OK;
}
