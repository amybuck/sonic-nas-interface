
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
 * filename: nas_int_ev_handlers.cpp
 * Provides Event handlers for PHY, VLAN and LAG interfaces for the events coming
 * from OS.
 */

#include "event_log.h"
#include "std_assert.h"
#include "hal_interface_common.h"
#include "hal_if_mapping.h"
#include "nas_int_utils.h"
#include "nas_int_vlan.h"
#include "nas_os_interface.h"
#include "nas_int_bridge.h"
#include "nas_int_lag.h"
#include "nas_int_lag_api.h"
#include "nas_int_com_utils.h"
#include "std_error_codes.h"
#include "cps_api_object_category.h"
#include "cps_api_interface_types.h"
#include "dell-base-if-linux.h"
#include "dell-base-if-vlan.h"
#include "dell-base-if.h"
#include "dell-interface.h"
#include "ietf-interfaces.h"
#include "dell-base-common.h"

#include "cps_api_object_key.h"
#include "cps_api_operation.h"
#include "ds_common_types.h"
#include "cps_class_map.h"
#include "std_mac_utils.h"

#include <unordered_map>
#include <string.h>


// TODO can be moved into a utility file.
static t_std_error get_port_list_by_ifindex(nas_port_list_t &port_list, cps_api_object_t obj, cps_api_attr_id_t attr_id)
{
    cps_api_object_it_t it;
    cps_api_object_it_begin(obj, &it);
    for (;cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        cps_api_attr_id_t id = cps_api_object_attr_id(it.attr);
        if (id == attr_id ) {
            port_list.insert(cps_api_object_attr_data_u32(it.attr));
        }
    }
    return STD_ERR_OK;
}

/*  Add or delete port in a bridge interface */
static void nas_vlan_ev_handler(cps_api_object_t obj) {

    nas_port_list_t tag_port_list;
    nas_port_list_t untag_port_list;
     cps_api_object_attr_t _br_idx_attr = cps_api_object_attr_get(obj, BASE_IF_LINUX_IF_INTERFACES_INTERFACE_IF_MASTER);

    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    if (_br_idx_attr == nullptr) {
        EV_LOG(ERR,INTERFACE, ev_log_s_MAJOR, "NAS-Vlan", "VLAN event received without bridge and port info");
        return;
    }
    hal_ifindex_t br_ifindex = cps_api_object_attr_data_u32(_br_idx_attr);

    get_port_list_by_ifindex(tag_port_list, obj, DELL_IF_IF_INTERFACES_INTERFACE_TAGGED_PORTS);
    get_port_list_by_ifindex(untag_port_list, obj, DELL_IF_IF_INTERFACES_INTERFACE_UNTAGGED_PORTS);
    if (tag_port_list.empty()  && untag_port_list.empty()) {
        EV_LOG(ERR,INTERFACE, ev_log_s_MAJOR, "NAS-Vlan", "VLAN event received without bridge and port info");
    }

    if (op == cps_api_oper_DELETE) {
        // Delete a port from vlan
        EV_LOG(INFO,INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                    "Delete interface list from bridge Interface %d\n", br_ifindex);
        if(!untag_port_list.empty()) {
            nas_process_del_vlan_mem_from_os(br_ifindex, untag_port_list, NAS_PORT_UNTAGGED);
        }
        if(!tag_port_list.empty()) {
            nas_process_del_vlan_mem_from_os(br_ifindex, tag_port_list, NAS_PORT_TAGGED);
        }
        return;
    } else if (op == cps_api_oper_CREATE) {
        hal_vlan_id_t vlan_id = 0;

        cps_api_object_attr_t vlan_id_attr = cps_api_object_attr_get(obj, BASE_IF_VLAN_IF_INTERFACES_INTERFACE_ID);
        if (vlan_id_attr != nullptr) {
            vlan_id = cps_api_object_attr_data_u32(vlan_id_attr);
        }
        if (vlan_id >IF_VLAN_MAX) {
            EV_LOG(ERR,INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                       "Invalid vlan %d to be added on bridge interface %d",
                       vlan_id, br_ifindex);
            return;
        }
        //TODO following function can be merged with the CPS handling function.
        nas_process_add_vlan_mem_from_os(br_ifindex, untag_port_list, 0, NAS_PORT_UNTAGGED);
        nas_process_add_vlan_mem_from_os(br_ifindex, tag_port_list, vlan_id, NAS_PORT_TAGGED);
    }
    return;
}

void nas_lag_ev_handler(cps_api_object_t obj) {

    nas_lag_id_t lag_id = 0; // @TODO for now lag_id=0
    hal_ifindex_t mem_idx;
    const char *mem_name = NULL;
    nas_lag_master_info_t *nas_lag_entry=NULL;
    cps_api_operation_types_t member_op = cps_api_oper_NULL;

    cps_api_object_attr_t _idx_attr = cps_api_object_attr_get(obj,
                                        DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX);
    cps_api_object_attr_t _mem_attr = cps_api_object_attr_get(obj,
                                     DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS_NAME);
    cps_api_object_attr_t _name_attr = cps_api_object_attr_get(obj, IF_INTERFACES_INTERFACE_NAME);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    if (_idx_attr == nullptr) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-LAG", " LAG IF_index not present in the OS EVENT");
        return;
    }
    hal_ifindex_t bond_idx = cps_api_object_attr_data_u32(_idx_attr);
    if (_mem_attr != nullptr) {
        mem_name =  (const char*)cps_api_object_attr_data_bin(_mem_attr);
        if (nas_int_name_to_if_index(&mem_idx, mem_name) != STD_ERR_OK) {
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-LAG",
                    " Failed for get member %s if_index ", mem_name);
            return;
        }
    }
    std_mutex_simple_lock_guard lock_t(nas_lag_mutex_lock());
    /* If op is CREATE then create the lag if not present and then add member lists*/
    if (op == cps_api_oper_CREATE) {
        if (_name_attr == nullptr) {
            EV_LOG(ERR, INTERFACE, 0, "NAS-LAG", " Bond name is not present in the create event");
            return;
        }
        const char *bond_name =  (const char*)cps_api_object_attr_data_bin(_name_attr);
        if ((nas_lag_entry = nas_get_lag_node(bond_idx)) == NULL) {
            /*  Create the lag  */
            EV_LOG(INFO,INTERFACE,3,"NAS-LAG","Create Lag interface idx %d with lag ID %d ", bond_idx,lag_id);

            if ((nas_lag_master_add(bond_idx,bond_name,lag_id)) != STD_ERR_OK) {
                    EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-LAG",
                            "LAG creation Failed for bond interface %s index %d",bond_name, bond_idx);
                    return;
            }
            /* Now get the lag node entry */
            nas_lag_entry = nas_get_lag_node(bond_idx);
        }
        /*   Check if Member port is present then add the members in the lag */
        if (_mem_attr != nullptr) {
            if(nas_lag_member_add(bond_idx,mem_idx,lag_id) != STD_ERR_OK) {
                EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-LAG",
                    "Failed to Add member %s to the Lag %s", mem_name, bond_name);
                return ;
            }
            member_op = cps_api_oper_SET;
            ndi_port_t ndi_port;
            if (nas_int_get_npu_port(mem_idx, &ndi_port) != STD_ERR_OK) {
                EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-LAG",
                        "Error in finding member's %s npu port info", mem_name);
                return;
            }
            ndi_intf_link_state_t state;
            if ((ndi_port_link_state_get(ndi_port.npu_id, ndi_port.npu_port, &state))
                     == STD_ERR_OK) {
                    /* if port is not UP then add in the block list */
                if (nas_lag_block_port(nas_lag_entry,mem_idx,
                       (state.oper_status == ndi_port_OPER_UP) ? false : true) != STD_ERR_OK){
                    EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                            "Error Block/unblock Port %s lag %s ",mem_name, bond_name);
                return ;
                }
            }
        }
        /* config Admin and MAC attribute */
        cps_api_object_attr_t _mac = cps_api_object_attr_get(obj,
                                                    DELL_IF_IF_INTERFACES_INTERFACE_PHYS_ADDRESS);
        if (_mac != nullptr) {
            nas_lag_set_mac(bond_idx, (const char *)cps_api_object_attr_data_bin(_mac));
        }
        cps_api_object_attr_t _enabled = cps_api_object_attr_get(obj, IF_INTERFACES_INTERFACE_ENABLED);
        if (_enabled != nullptr) {
            nas_lag_set_admin_status(bond_idx, (bool)cps_api_object_attr_data_u32(_enabled));
        }
    } else if (op == cps_api_oper_DELETE) { /* If op is DELETE */
        if (_mem_attr != nullptr) {
            /*  delete the member from the lag */
            if(nas_lag_member_delete(bond_idx, mem_idx,lag_id) != STD_ERR_OK) {
                EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-LAG",
                    "Failed to Delete member %s to the Lag %d", mem_name, bond_idx);
                return ;
            }
            member_op = cps_api_oper_SET;
        } else {
            /*  Otherwise the event is to delete the lag */
            if((nas_lag_master_delete(bond_idx) != STD_ERR_OK))
                return ;
        }
    }
    if (member_op == cps_api_oper_SET) {
        /*  Publish the Lag event with portlist in case of member addition/deletion */
        if ((nas_lag_entry = nas_get_lag_node(bond_idx)) == NULL) {
            return;
        }
        if(lag_object_publish(nas_lag_entry, bond_idx, member_op)!= cps_api_ret_code_OK){
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                    "LAG events publish failure");
            return ;
        }
    }
}

// Bridge Event Handler
static void nas_bridge_ev_handler(cps_api_object_t obj)
{
    bool create= false;
    nas_bridge_t *b_node = NULL;

    cps_api_object_attr_t _idx_attr = cps_api_object_attr_get(obj, DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));
    cps_api_object_attr_t _name_attr = cps_api_object_attr_get(obj, IF_INTERFACES_INTERFACE_NAME);

    if ((_idx_attr == nullptr) || (_name_attr == nullptr))  {
        return;
    }
    hal_ifindex_t if_index = cps_api_object_attr_data_u32(_idx_attr);
    const char *name =  (const char*)cps_api_object_attr_data_bin(_name_attr);

    EV_LOG(INFO,INTERFACE, ev_log_s_MINOR, "NAS-Br", "Bridge Interface %d, name %s, operation %d",
                            if_index, name, op);

    if (op == cps_api_oper_DELETE) {
        if(nas_delete_bridge(if_index) != STD_ERR_OK)
            return;
    } else {
        /* Moving the locks from the following function to here.
         * In case of Dell CPS, often the netlink Rx processing kicks in before the
         * netlink set returns the context back to caller function */
        nas_bridge_lock();
        b_node = nas_create_insert_bridge_node(if_index, name, create);
        if (b_node == NULL) {
            nas_bridge_unlock();
            return;
        }
        if (create == true ) {
            nas_handle_bridge_mac(b_node, if_index);
        }
        nas_bridge_unlock();
    }
    return;
}

void nas_send_admin_state_event(hal_ifindex_t if_index, IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_t state)
{

    char buff[CPS_API_MIN_OBJ_LEN];
    cps_api_object_t obj = cps_api_object_init(buff,sizeof(buff));
    if (!cps_api_key_from_attr_with_qual(cps_api_object_key(obj),
                DELL_BASE_IF_CMN_IF_INTERFACES_STATE_INTERFACE_OBJ,
                cps_api_qualifier_OBSERVED)) {
        EV_LOG(ERR,INTERFACE,0,"NAS-IF-REG","Could not translate to logical interface key ");
        return;
    }
    cps_api_object_attr_add_u32(obj, IF_INTERFACES_STATE_INTERFACE_IF_INDEX, if_index);
    cps_api_object_attr_add_u32((obj), IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS, state);
    hal_interface_send_event(obj);
}

void nas_int_ev_handler(cps_api_object_t obj) {

    hal_ifindex_t if_index;
    ndi_port_t ndi_port;
    cps_api_object_attr_t attr = cps_api_object_attr_get(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX);
    if (attr==nullptr) return ; // if index not present
    if_index = cps_api_object_attr_data_u32(attr);

    if (nas_int_get_npu_port(if_index, &ndi_port) != STD_ERR_OK) {
        EV_LOG_ERR(ev_log_t_INTERFACE,1,"NAS-INT","if_index %d is wrong or Interface has NO slot port ", if_index);
        return;
    }
    /*
     * Admin State
     */
    attr = cps_api_object_attr_get(obj,IF_INTERFACES_INTERFACE_ENABLED);
    if (attr!=nullptr) {
        IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_t state, current_state;
        state = (bool)cps_api_object_attr_data_u32(attr) ?
                IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_UP : IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_DOWN;
        if (ndi_port_admin_state_get(ndi_port.npu_id, ndi_port.npu_port,&current_state)==STD_ERR_OK) {
            if (current_state != state) {
                if (ndi_port_admin_state_set(ndi_port.npu_id, ndi_port.npu_port,
                            (state == IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_UP) ? true: false) != STD_ERR_OK) {
                    EV_LOG_ERR(ev_log_t_INTERFACE,1,"INTF-NPU","Error Setting Admin State in NPU");
                } else {
                    EV_LOG(INFO,INTERFACE,3,"INTF-NPU","Admin state change on %d:%d to %d",
                        ndi_port.npu_id, ndi_port.npu_port,state);
                }
                nas_send_admin_state_event(if_index, state);
            }
        } else {
            /*  port admin state get error */
                EV_LOG_ERR(ev_log_t_INTERFACE,1,"INTF-NPU","Admin state Get failed on %d:%d ",
                    ndi_port.npu_id, ndi_port.npu_port);
        }
    }
    /*
     * handle ip mtu
     */
    attr = cps_api_object_attr_get(obj,DELL_IF_IF_INTERFACES_INTERFACE_MTU);
    if (attr!=NULL) {
        uint_t mtu = cps_api_object_attr_data_u32(attr);
        auto npu = ndi_port.npu_id;
        auto port = ndi_port.npu_port;

        if (ndi_port_mtu_set(npu,port, mtu)!=STD_ERR_OK) {
            /* If unable to set new port MTU (based on received MTU) in NPU
               then revert back the MTU in kernel and MTU in NPU to old values */
            EV_LOG_ERR(ev_log_t_INTERFACE,1,"INTF-NPU","Error setting MTU %d in NPU\n", mtu);
            if (ndi_port_mtu_get(npu,port,&mtu)==STD_ERR_OK) {
                cps_api_set_key_data(obj, DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX, cps_api_object_ATTR_T_U32,
                             &if_index, sizeof(if_index));
                cps_api_object_attr_delete(obj,DELL_IF_IF_INTERFACES_INTERFACE_MTU);
                cps_api_object_attr_add_u32(obj,DELL_IF_IF_INTERFACES_INTERFACE_MTU,mtu);
                nas_os_interface_set_attribute(obj,DELL_IF_IF_INTERFACES_INTERFACE_MTU);
            }
        }
    }
}

static const std::unordered_map<BASE_CMN_INTERFACE_TYPE_t, void(*)(cps_api_object_t), std::hash<int>> _int_ev_handlers = {
                       { BASE_CMN_INTERFACE_TYPE_L3_PORT, nas_int_ev_handler},
                       { BASE_CMN_INTERFACE_TYPE_L2_PORT, nas_bridge_ev_handler},
                       { BASE_CMN_INTERFACE_TYPE_VLAN, nas_vlan_ev_handler},
                       { BASE_CMN_INTERFACE_TYPE_LAG, nas_lag_ev_handler},
};
bool nas_int_ev_handler_cb(cps_api_object_t obj, void *param) {

    cps_api_object_attr_t _type = cps_api_object_attr_get(obj,BASE_IF_LINUX_IF_INTERFACES_INTERFACE_DELL_TYPE);
    if (_type==nullptr) {
        EV_LOG_ERR(ev_log_t_INTERFACE,1,"INTF-EV","Unknown Event or interface type not present");
        return false;
    }
    BASE_CMN_INTERFACE_TYPE_t if_type = (BASE_CMN_INTERFACE_TYPE_t) cps_api_object_attr_data_u32(_type);
    auto func = _int_ev_handlers.find(if_type);
    if (func != _int_ev_handlers.end()) {
        func->second(obj);
        return true;
    } else {
        EV_LOG_ERR(ev_log_t_INTERFACE,1,"INTF-EV","Unknown interface type");
        return false;
    }
}
