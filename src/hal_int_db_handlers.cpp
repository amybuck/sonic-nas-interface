
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
 * filename: hal_int_db_handlers.cpp
 */

/* ******** DEPRECATED and replaced with nas_int_ev_handlers.cpp ****/

#include "event_log.h"
#include "std_assert.h"
#include "hal_interface_common.h"
#include "hal_if_mapping.h"
#include "nas_int_vlan.h"
#include "nas_os_interface.h"
#include "nas_int_bridge.h"
#include "nas_int_lag.h"
#include "nas_int_lag_api.h"
#include "std_error_codes.h"
#include "cps_api_object_category.h"
#include "cps_api_interface_types.h"
#include "dell-base-if.h"
#include "ietf-interfaces.h"

#include "cps_api_object_key.h"
#include "cps_api_operation.h"
#include "ds_common_types.h"
#include "cps_class_map.h"
#include "std_mac_utils.h"

#include <string.h>

extern "C" {

const unsigned int mac_str_len =20;

static bool hal_int_update_vlan(cps_api_object_t obj) {

    cps_api_object_attr_t list[cps_api_if_STRUCT_A_MAX];
    hal_ifindex_t port_idx = 0;

    cps_api_object_attr_fill_list(obj,0,list,sizeof(list)/sizeof(*list));

    if (list[cps_api_if_STRUCT_A_IFINDEX]==NULL ||
        list[cps_api_if_STRUCT_A_OPERATION]==NULL ||
        list[cps_api_if_STRUCT_A_MASTER] == NULL) {
        EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                   "Parameter is missing for VLAN add/del");
        return false;
    }

    uint_t if_index = cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_IFINDEX]);
    uint_t operation = cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_OPERATION]);
    hal_ifindex_t br_ifindex = (hal_ifindex_t)cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_MASTER]);
    if (list[cps_api_if_STRUCT_A_VLAN_PORT_INDEX] != NULL) {
       port_idx = cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_VLAN_PORT_INDEX]);
    }
    if (operation == DB_INTERFACE_OP_DELETE) {
        EV_LOG(INFO,INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                    "Delete VLAN Interface %d\n", if_index);
        nas_process_delete_vlan(br_ifindex, if_index, port_idx);
    } else {
        hal_vlan_id_t vlan_id = 0;
        if (list[cps_api_if_STRUCT_A_VLAN_PORT_INDEX]==NULL) {
            EV_LOG(ERR,INTERFACE,ev_log_s_CRITICAL, "NAS-Vlan",
                       "Parameter is missing for VLAN add/del");
            return false;
        }

        port_idx = cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_VLAN_PORT_INDEX]);

        if(list[cps_api_if_STRUCT_A_VLAN_ID] != NULL) {
            vlan_id = cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_VLAN_ID]);

            if (vlan_id >IF_VLAN_MAX) {
                EV_LOG(ERR,INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                           "Invalid vlan %d to be added on interface %d",
                           vlan_id, if_index);
                return false;
            }
        }

        EV_LOG(INFO,INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                   "Interface %d has port %d vlan %d bridge %d",
                    if_index, port_idx, vlan_id, br_ifindex);

        if(nas_process_add_vlan(br_ifindex, if_index, port_idx, vlan_id)
                                != STD_ERR_OK)
            return false;
    }
    return true;
}

void nas_int_update_lag(cps_api_object_t obj) {

    cps_api_object_attr_t list[cps_api_if_STRUCT_A_MAX];
    cps_api_object_attr_fill_list(obj,0,list,sizeof(list)/sizeof(*list));
    nas_lag_id_t lag_id = 0; // @TODO for now lag_id=0
    uint_t operation;
    uint_t if_flags;
    hal_ifindex_t lag_master_index;
    nas_lag_master_info_t *nas_lag_entry=NULL;
    cps_api_operation_types_t op;

    if (list[cps_api_if_STRUCT_A_IFINDEX]==NULL ||
            list[cps_api_if_STRUCT_A_FLAGS]==NULL||
            list[cps_api_if_STRUCT_A_OPERATION]==NULL){

        EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-INTF-LAG",
                "Missing parameters");
        return ;
    }

    hal_ifindex_t  if_index = cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_IFINDEX]);
    operation = cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_OPERATION]);
    if_flags = cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_FLAGS]);
    const char *if_name =  (char*)cps_api_object_attr_data_bin(list[cps_api_if_STRUCT_A_NAME]);

    std_mutex_simple_lock_guard lock_t(nas_lag_mutex_lock());
    if (list[cps_api_if_STRUCT_A_MASTER] != NULL){
        lag_master_index = (hal_ifindex_t)cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_MASTER]);
    } else {
        lag_master_index = if_index;
    }


    EV_LOG(INFO,INTERFACE,3,"NAS-INTF","NAS-INTF-LAG mutex lock context");

    if (((operation == DB_INTERFACE_OP_DELETE)) && (is_master(if_flags))) {

        EV_LOG(INFO,INTERFACE,3,"NAS-INTF","Delete Lag master idx %d with lag ID %d ifindex %d ",
                lag_master_index,lag_id,if_index);

        if((nas_lag_master_delete(if_index) != STD_ERR_OK))
            return ;
    }

    if (((operation == DB_INTERFACE_OP_CREATE))
            && (is_master(if_flags))) {
        t_std_error rc = STD_ERR_OK;
        if (is_intf_up(if_flags)) {
            EV_LOG(INFO,INTERFACE,3,"NAS-INTF","Create Lag master idx %d with lag ID %d ifindex %d ",
                    lag_master_index,lag_id,if_index);

            rc = nas_lag_master_add(if_index,if_name,lag_id);
        }

        if (rc != STD_ERR_OK) {
            return;
        }

        if (list[cps_api_if_STRUCT_A_IF_MACADDR] != NULL) {
            void *data = cps_api_object_attr_data_bin(list[cps_api_if_STRUCT_A_IF_MACADDR]);
            char mac_str[mac_str_len];
            std_mac_to_string((const hal_mac_addr_t *)data,mac_str,mac_str_len);
            nas_lag_set_mac(if_index, mac_str);
        }

        if (list[cps_api_if_STRUCT_A_ADMIN_STATE] != NULL) {
            uint32_t state = cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_ADMIN_STATE]);
            nas_lag_set_admin_status(if_index, (IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_t)state);
        }
    }

    if (((operation == DB_INTERFACE_OP_CREATE)) && (is_slave(if_flags))
            && (list[cps_api_if_STRUCT_A_MASTER] != NULL)) {

        EV_LOG(INFO,INTERFACE,3,"NAS-INTF","Add Mem if_index %d lag_index %d slave flags %x",
                if_index,lag_master_index,if_flags);

        if(nas_lag_member_add(lag_master_index,if_index,lag_id) != STD_ERR_OK)
            return ;

        op = cps_api_oper_SET;

        nas_lag_entry = nas_get_lag_node(lag_master_index);

        if(nas_lag_entry == NULL){
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                    "Lag node is NULL");
            return ;
        }

        bool port_state = true;
        if(if_flags & NAS_IFF_RUNNING){
            port_state = false;
            op = cps_api_oper_NULL;
        }

        EV_LOG(INFO, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG","port_state %d and iff_flags%x",port_state,if_flags);

        if(nas_lag_block_port(nas_lag_entry,if_index,port_state) != STD_ERR_OK){
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                    "Error Block/unblock Port %d",if_index);
            return ;
        }
    }

    if (((operation == DB_INTERFACE_OP_CREATE)) && (is_slave(if_flags))
            && (list[cps_api_if_STRUCT_A_MASTER] == NULL)) {
        EV_LOG(INFO,INTERFACE,3,"NAS-INTF","Delete Lag Interface %d with lag ID %d ifindex %d if_flags %x",
                lag_master_index,lag_id,if_index,if_flags);

        lag_master_index = nas_get_master_idx(if_index);
        if(nas_lag_member_delete(lag_master_index, if_index,lag_id) != STD_ERR_OK)
            return ;
        op = cps_api_oper_SET;
        // Get the lag object
        nas_lag_entry = nas_get_lag_node(lag_master_index);

        if(nas_lag_entry == NULL){
            EV_LOG(ERR,INTERFACE,3, "NAS-LAG", "Lag intf %d node is Null",
                    lag_master_index);
            return ;
        }
    }

    // publish CPS_LAG object for add/delete of port
    if (op == cps_api_oper_SET){

        EV_LOG(INFO,INTERFACE,3,"NAS-INTF","NAS-CPS-LAG","OS LAG Publish Event %d",lag_master_index);

        if(lag_object_publish(nas_lag_entry, lag_master_index, op)!= cps_api_ret_code_OK){
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                    "LAG events publish failure");
            return ;
        }
    }

}



static bool hal_int_update_bridge(cps_api_object_t obj) {

    cps_api_object_attr_t list[cps_api_if_STRUCT_A_MAX];
    cps_api_object_attr_fill_list(obj,0,list,sizeof(list)/sizeof(*list));

    if (list[cps_api_if_STRUCT_A_IFINDEX]==NULL ||
            list[cps_api_if_STRUCT_A_NAME]==NULL||
            list[cps_api_if_STRUCT_A_OPERATION]==NULL)
        return false;

    hal_ifindex_t if_index = cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_IFINDEX]);
    uint_t operation = cps_api_object_attr_data_u32(list[cps_api_if_STRUCT_A_OPERATION]);
    const char *name =  (char*)cps_api_object_attr_data_bin(list[cps_api_if_STRUCT_A_NAME]);

    EV_LOG(INFO,INTERFACE, ev_log_s_MINOR, "NAS-Br",
               "Bridge Interface %d, name %s, operation %d",
                if_index, name, operation);

    if (operation== DB_INTERFACE_OP_DELETE) {
        if(nas_delete_bridge(if_index) != STD_ERR_OK)
            return false;
    } else {
        /* Moving the locks from the following function to here.
         * In case of Dell CPS, often the netlink Rx processing kicks in before the
         * netlink set returns the context back to caller function */
        nas_bridge_lock();
        if(nas_create_insert_bridge_node(if_index, name) == NULL) {
            nas_bridge_unlock();
            return false;
        }
        nas_bridge_unlock();
    }
    return true;
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

void hal_int_update_hdlr(cps_api_object_t obj) {
    interface_ctrl_t intf_ctrl;

    memset(&intf_ctrl, 0, sizeof(interface_ctrl_t));
    intf_ctrl.q_type = HAL_INTF_INFO_FROM_IF;

    cps_api_object_attr_t attr = cps_api_object_attr_get(obj,cps_api_if_STRUCT_A_IFINDEX);
    if (attr==NULL) return ;
    intf_ctrl.if_index = cps_api_object_attr_data_u32(attr);

    // get the BCM slot_id/port_id from ifindex
    if (dn_hal_get_interface_info(&intf_ctrl) != STD_ERR_OK) {
        EV_LOG_ERR(ev_log_t_INTERFACE,1,"INTF-LOW-BCM","Interface %d has NO slot %d, port %d",
                intf_ctrl.if_index , intf_ctrl.npu_id, intf_ctrl.port_id);
        return;
    }

    /*
     * Admin State
     */

    attr = cps_api_object_attr_get(obj,cps_api_if_STRUCT_A_ADMIN_STATE);
    if (attr!=NULL) {
        IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_t state, current_state;
        state = (db_interface_state_t)cps_api_object_attr_data_u32(attr) == DB_ADMIN_STATE_UP ?
                IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_UP : IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_DOWN;
        if (ndi_port_admin_state_get(intf_ctrl.npu_id, intf_ctrl.port_id,&current_state)==STD_ERR_OK) {
            if (current_state != state) {
                if (ndi_port_admin_state_set(intf_ctrl.npu_id, intf_ctrl.port_id,
                            (state == IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_UP) ? true: false) != STD_ERR_OK) {
                    EV_LOG_ERR(ev_log_t_INTERFACE,1,"INTF-NPU","Error Setting Admin State in NPU");
                } else {
                    EV_LOG(INFO,INTERFACE,3,"INTF-NPU","Admin state change on %d:%d to %d",
                        intf_ctrl.npu_id, intf_ctrl.port_id,state);
                }
                nas_send_admin_state_event(intf_ctrl.if_index, state);
            }
        } else {
            /*  port admin state get error */
                EV_LOG_ERR(ev_log_t_INTERFACE,1,"INTF-NPU","Admin state Get failed on %d:%d ",
                    intf_ctrl.npu_id, intf_ctrl.port_id);
        }
    }

    /*
     * handle ip mtu
     */
    attr = cps_api_object_attr_get(obj,cps_api_if_STRUCT_A_MTU);
    if (attr!=NULL) {
        uint_t mtu = cps_api_object_attr_data_u32(attr);
        auto npu = intf_ctrl.npu_id;
        auto port = intf_ctrl.port_id;

        EV_LOG(INFO, INTERFACE, 3,
               "INTF-NPU", "MTU change on %d:%d to %d", npu, port, mtu);
        if (ndi_port_mtu_set(npu,port, mtu)!=STD_ERR_OK) {
            /* If unable to set new port MTU (based on received MTU) in NPU
               then revert back the MTU in kernel and MTU in NPU to old values */
            EV_LOG_ERR(ev_log_t_INTERFACE,1,"INTF-NPU","Error setting MTU %d in NPU\n", mtu);
            if (ndi_port_mtu_get(npu,port,&mtu)==STD_ERR_OK) {
                cps_api_set_key_data(obj, BASE_PORT_INTERFACE_IFINDEX, cps_api_object_ATTR_T_U32,
                             &intf_ctrl.if_index, sizeof(intf_ctrl.if_index));
                cps_api_object_attr_delete(obj,BASE_PORT_INTERFACE_MTU);
                cps_api_object_attr_add_u32(obj,BASE_PORT_INTERFACE_MTU,mtu);
                nas_os_interface_set_attribute(obj,BASE_PORT_INTERFACE_MTU);
            }
        }
    }
    /*
     * handle speed XXX TODO
     */

}

bool hal_int_event_function_cb(cps_api_object_t obj, void *param) {

    if (cps_api_key_get_cat(cps_api_object_key(obj)) != cps_api_obj_cat_INTERFACE) {
        /// don't care.. not registered any other for now
        return true;
    }

    cps_api_object_attr_t type = cps_api_object_attr_get(obj,cps_api_if_STRUCT_A_IF_TYPE);

    if (type!=NULL) {
        uint32_t if_type = cps_api_object_attr_data_u32(type);

        if (if_type==DB_IF_TYPE_VLAN_INTER) {
            EV_LOG_INFO(ev_log_t_INTERFACE, 3,"NAS-INTF", "ADD/DEL Vlan \n");

            return (hal_int_update_vlan(obj));
        }

        if(if_type==DB_IF_TYPE_BRIDGE_INTER) {
                EV_LOG_INFO(ev_log_t_INTERFACE, 3,"NAS-INTF",
                            "ADD/DEL bridge \n");
                return (hal_int_update_bridge(obj));
        }
    }
    if (type!=NULL &&
            (cps_api_object_attr_data_u32(type)==DB_IF_TYPE_LAG_INTER)) {
            nas_int_update_lag(obj);
    }

    hal_int_update_hdlr(obj);
    return true;
}


}
