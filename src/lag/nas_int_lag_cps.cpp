
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
 * nas_lag_api.cpp
 */

#include "dell-base-if-lag.h"
#include "dell-base-if.h"
#include "dell-interface.h"
#include "nas_os_lag.h"
#include "nas_os_interface.h"
#include "nas_int_port.h"
#include "cps_api_events.h"
#include "event_log.h"
#include "event_log_types.h"
#include "nas_int_lag_api.h"
#include "nas_int_lag_cps.h"
#include "nas_int_lag.h"
#include "std_mac_utils.h"
#include "nas_ndi_obj_id_table.h"
#include "interface_obj.h"
#include "nas_int_utils.h"
#include "hal_interface_common.h"

#include <stdio.h>

#include "cps_class_map.h"
#include "cps_api_events.h"
#include "cps_api_object_key.h"
#include <unordered_set>


const static int MAX_CPS_MSG_BUFF=4096;

typedef std::unordered_set <hal_ifindex_t> nas_port_list_t;
static cps_api_return_code_t nas_cps_set_lag(cps_api_object_t obj);


static bool nas_lag_get_ifindex_from_obj(cps_api_object_t obj,hal_ifindex_t *index){
    cps_api_object_attr_t lag_name_attr = cps_api_get_key_data(obj, IF_INTERFACES_INTERFACE_NAME);
    cps_api_object_attr_t lag_attr = cps_api_object_attr_get(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX );

    if(lag_attr == NULL && lag_name_attr == NULL) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
            "Missing Name/ifindex attribute for CPS Set");
        return false;
    }

    if(lag_attr){
        *index = (hal_ifindex_t) cps_api_object_attr_data_u32(lag_attr);
    }else{
        const char * name = (const char *)cps_api_object_attr_data_bin(lag_name_attr);
        interface_ctrl_t i;
        memset(&i,0,sizeof(i));
        strncpy(i.if_name,name,sizeof(i.if_name)-1);
        i.q_type = HAL_INTF_INFO_FROM_IF_NAME;
        if (dn_hal_get_interface_info(&i)!=STD_ERR_OK){
            EV_LOG(ERR, INTERFACE, 0, "NAS-CPS-LAG",
                    "Can't get interface control information for %s",name);
            return false;
        }
        *index = i.if_index;
    }
    return true;
}


static inline bool nas_get_lag_id_from_str(const char * str, nas_lag_id_t * id){
    std::string full_str(str);
    std::size_t pos = full_str.find_first_of("0123456789");
    std::string id_str = full_str.substr(pos);
    *id = std::stoi(id_str);
    return true;
}

static cps_api_return_code_t nas_cps_create_lag(cps_api_object_t obj)
{
    hal_ifindex_t lag_index = 0;
    cps_api_return_code_t rc =cps_api_ret_code_OK;
    nas_lag_id_t lag_id=0;
    cps_api_object_attr_t lag_id_attr = cps_api_get_key_data(obj, IF_INTERFACES_INTERFACE_NAME);

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "Create Lag using CPS");

    if(lag_id_attr == NULL) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "Missing Lag Name ");
        return cps_api_ret_code_ERR;
    }

    const char * name = (const char *)cps_api_object_attr_data_bin(lag_id_attr);

    if(nas_lag_get_ifindex_from_obj(obj,&lag_index)){
        EV_LOG(INFO,INTERFACE,3,"NAS-CPS-LAG","Lag %s already exist",name);
        return nas_cps_set_lag(obj);
    }

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG","Create Lag %s in kernel",name);

    //Acquring lock to avoid pocessing netlink msg from kernel.
    std_mutex_simple_lock_guard lock_t(nas_lag_mutex_lock());

    if((nas_os_create_lag(obj, &lag_index)) != STD_ERR_OK) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "Failure creating Lag in Kernel");
        return cps_api_ret_code_ERR;
    }

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "Kernel programmed, creating lag with index %d", lag_index);

    nas_get_lag_id_from_str(name,&lag_id);

    if(nas_lag_master_add(lag_index,name,lag_id) != STD_ERR_OK)
    {
        EV_LOG(INFO, INTERFACE, ev_log_s_MINOR,"NAS-CPS-LAG",
                "Failure in NAS-CPS-LAG (NPU) creating lag with index %d", lag_index);
        return cps_api_ret_code_ERR;
    }

    cps_api_set_key_data(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX,cps_api_object_ATTR_T_U32,
            &lag_index,sizeof(lag_index));

    if(nas_cps_set_lag(obj)!=cps_api_ret_code_OK)
    {
        EV_LOG(INFO, INTERFACE, ev_log_s_MINOR,"NAS-CPS-LAG",
                "Failure in NAS-CPS-LAG Set operation in Create Lag index %d", lag_index);
        return cps_api_ret_code_ERR;
    }

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "Exit Create Lag using CPS");
    return rc;
}

static cps_api_return_code_t nas_cps_delete_lag(cps_api_object_t obj)
{
    cps_api_return_code_t rc = cps_api_ret_code_OK;

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "CPS Delete LAG");

    hal_ifindex_t lag_index = 0;
    if(!nas_lag_get_ifindex_from_obj(obj,&lag_index)){
        return cps_api_ret_code_ERR;
    }

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "Deleting Lag Intf %d", lag_index);

    cps_api_object_attr_add_u32(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX,lag_index);
    cps_api_object_attr_t type = cps_api_object_attr_get(obj,DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS);
    cps_api_object_attr_t member_port_attr = cps_api_get_key_data(obj, DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS_NAME);

    if(type || member_port_attr){
        EV_LOG(INFO,INTERFACE,3,"NAS-CPS-LAG","Remove Member Port for Lag %d",lag_index);
        return nas_cps_set_lag(obj);
    }

    //Acquring lock to avoid pocessing netlink msg from kernel.
    std_mutex_simple_lock_guard lock_t(nas_lag_mutex_lock());

    if(nas_os_delete_lag(obj) != STD_ERR_OK) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "Failure deleting LAG index from kernel");
        return cps_api_ret_code_ERR;
    }

    if(nas_lag_master_delete(lag_index) != STD_ERR_OK){
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "Failure deleting LAG index from Nas-lag(NPU)");
        return cps_api_ret_code_ERR;
    }

    return rc;
}

static cps_api_return_code_t nas_cps_set_mac(cps_api_object_t obj,hal_ifindex_t lag_index)
{
    cps_api_object_attr_t attr = cps_api_object_attr_get(obj, DELL_IF_IF_INTERFACES_INTERFACE_PHYS_ADDRESS);
    if (attr==NULL) return cps_api_ret_code_ERR;

    cps_api_set_key_data(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX,cps_api_object_ATTR_T_U32,
            &lag_index,sizeof(lag_index));

    if(nas_os_interface_set_attribute(obj,DELL_IF_IF_INTERFACES_INTERFACE_PHYS_ADDRESS) != STD_ERR_OK)
    {
        EV_LOG(ERR,INTERFACE,ev_log_s_CRITICAL ,"NAS-CPS-LAG", "lag MAC set failure OS");
        return cps_api_ret_code_ERR;
    }

    if(nas_lag_set_mac(lag_index,(const char *)cps_api_object_attr_data_bin(attr)) != STD_ERR_OK)
    {
        EV_LOG(ERR,INTERFACE,ev_log_s_CRITICAL ,"NAS-CPS-LAG", "lag MAC failure in NAS");
        return cps_api_ret_code_ERR;
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_cps_set_admin_status(cps_api_object_t obj,hal_ifindex_t lag_index,
                                                       nas_lag_master_info_t *nas_lag_entry)
{

    cps_api_object_attr_t attr = cps_api_object_attr_get(obj,IF_INTERFACES_INTERFACE_ENABLED);

    if (attr == NULL){
        EV_LOG(ERR,INTERFACE,ev_log_s_CRITICAL ,"NAS-CPS-LAG","admin status %d attr is NULL ",
                lag_index);
        return cps_api_ret_code_ERR;
    }

    if(nas_os_interface_set_attribute(obj,IF_INTERFACES_INTERFACE_ENABLED) != STD_ERR_OK)
    {
        EV_LOG(ERR,INTERFACE,ev_log_s_CRITICAL ,"NAS-CPS-LAG","lag Admin status set failure OS");
        return cps_api_ret_code_ERR;
    }

    //Update Status in lag struct
    nas_lag_entry->admin_status = cps_api_object_attr_data_u32(attr);

    return cps_api_ret_code_OK;
}


static cps_api_return_code_t nas_cps_add_port_to_lag(nas_lag_master_info_t *nas_lag_entry, hal_ifindex_t port_idx)
{
    cps_api_return_code_t rc = cps_api_ret_code_OK;
    char buff[MAX_CPS_MSG_BUFF];

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "Add Ports to Lag");

    cps_api_object_t name_obj = cps_api_object_init(buff, sizeof(buff));
    cps_api_object_attr_add_u32(name_obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX, nas_lag_entry->ifindex);
    cps_api_object_attr_add_u32(name_obj,DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS, port_idx);

    if(nas_os_add_port_to_lag(name_obj) != STD_ERR_OK) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "Error adding port %d to lag  %d in the Kernel", port_idx,nas_lag_entry->ifindex);
        return cps_api_ret_code_ERR;
    }

    if(nas_lag_member_add(nas_lag_entry->ifindex,port_idx,0) != STD_ERR_OK)
    {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "Error inserting index %d in list", port_idx);
        return cps_api_ret_code_ERR;
    }

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "Add Ports to Lag Exit");
    return rc;
}

static cps_api_return_code_t nas_cps_delete_port_from_lag(hal_ifindex_t ifindex)
{
    char buff[MAX_CPS_MSG_BUFF];
    cps_api_return_code_t rc=cps_api_ret_code_OK;

    cps_api_object_t obj = cps_api_object_init(buff, sizeof(buff));
    cps_api_object_attr_add_u32(obj, DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS, ifindex);


    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "nas_cps_del_port_from_lag %d",ifindex);

    //delete the port from NPU
    if((nas_lag_member_delete(0,ifindex,0) != STD_ERR_OK)){
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "Error deleting interface %d from NPU", ifindex);
        return cps_api_ret_code_ERR;
    }

    //NPU delete done, now delete from Kernel
    if(nas_os_delete_port_from_lag(obj) != STD_ERR_OK) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "Error deleting interface %d from OS", ifindex);
        return cps_api_ret_code_ERR;
    }

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "nas_cps_del_port_from_lag Exit %d",ifindex);
    return rc;
}

static bool nas_lag_get_intf_ctrl_info(hal_ifindex_t index, interface_ctrl_t & i){
    memset(&i,0,sizeof(i));
    i.if_index = index;
    i.q_type = HAL_INTF_INFO_FROM_IF;
    if (dn_hal_get_interface_info(&i)!=STD_ERR_OK){
        EV_LOG(ERR,INTERFACE,0,"NAS-CPS-LAG","Failed to get intrerface control information for "
                "interface %d",index);
        return false;
    }
    return true;

}


static bool nas_lag_get_intf_ctrl_info(const char * name, interface_ctrl_t & i){
    memset(&i,0,sizeof(i));
    strncpy(i.if_name,name,sizeof(i.if_name)-1);
    i.q_type = HAL_INTF_INFO_FROM_IF_NAME;
    if (dn_hal_get_interface_info(&i)!=STD_ERR_OK){
        EV_LOG(ERR,INTERFACE,0,"NAS-CPS-LAG","Failed to get intrerface control information for "
                "interface %s",name);
        return false;
    }
    return true;

}


static cps_api_return_code_t cps_lag_update_ports(nas_lag_master_info_t  *nas_lag_entry,
        nas_port_list_t &port_index_list,bool add_ports)
{

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "cps_lag_update_ports %d",add_ports);

    for(auto it = port_index_list.begin() ; it != port_index_list.end() ; ++it){

        if((nas_lag_entry->port_list.find(*it) == nas_lag_entry->port_list.end())
                && (add_ports == true)) {

            EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
                    "Received new port add %d", *it);
            if(nas_cps_add_port_to_lag(nas_lag_entry, *it) != cps_api_ret_code_OK) {
                EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                        "Error adding port %d to OS/NPU", *it);
                return cps_api_ret_code_ERR;
            }
        }

        if((nas_lag_entry->port_list.find(*it) != nas_lag_entry->port_list.end())
                && (add_ports == false)) {

            EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
                    "Received new port delete %d", *it);
            if(nas_cps_delete_port_from_lag(*it) != cps_api_ret_code_OK) {
                EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                        "Error deleting port  %d from  OS/NPU", *it);
                return cps_api_ret_code_ERR;
            }
        }
    }

    return cps_api_ret_code_OK;
}


static void nas_pack_lag_port_list(cps_api_object_t obj,nas_lag_master_info_t *nas_lag_entry, int attr_id,bool get)
{
    bool port_mode, block_port_set = false, unblock_port_set = false;
    unsigned int list_id =0;
    for (auto it=nas_lag_entry->port_list.begin(); it!=nas_lag_entry->port_list.end(); ++it) {
        EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-LAG-CPS","port idx %d",*it);
        interface_ctrl_t i;
        if(!nas_lag_get_intf_ctrl_info(*it,i)) return;

        if(get){
            cps_api_attr_id_t ids[3] = {DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS,list_id++,
                                        DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS_NAME };
            const int ids_len = sizeof(ids)/sizeof(ids[0]);
            cps_api_object_e_add(obj,ids,ids_len,cps_api_object_ATTR_T_BIN,i.if_name,strlen(i.if_name)+1);
        }else{
            cps_api_object_attr_add_u32(obj, attr_id, *it);
        }

        if (nas_lag_get_port_mode(*it, port_mode) == STD_ERR_OK) {
            if (port_mode) {
                if(get){
                    cps_api_object_attr_add(obj,BASE_IF_LAG_IF_INTERFACES_INTERFACE_BLOCK_PORT_LIST,i.if_name,strlen(i.if_name)+1);
                }else{
                    cps_api_object_attr_add_u32(obj, BASE_IF_LAG_IF_INTERFACES_INTERFACE_BLOCK_PORT_LIST, *it);
                }
                block_port_set = true;
            } else {
                if(get){
                    cps_api_object_attr_add(obj,BASE_IF_LAG_IF_INTERFACES_INTERFACE_UNBLOCK_PORT_LIST,i.if_name,strlen(i.if_name)+1);
                }else{
                    cps_api_object_attr_add_u32(obj, BASE_IF_LAG_IF_INTERFACES_INTERFACE_UNBLOCK_PORT_LIST, *it);
                }
                unblock_port_set = true;
            }
        }
    }

    if(nas_lag_entry->port_list.size() == 0) {
        cps_api_object_attr_add(obj, attr_id, 0, 0);
    }

    if (!block_port_set) {
        cps_api_object_attr_add(obj, BASE_IF_LAG_IF_INTERFACES_INTERFACE_BLOCK_PORT_LIST, 0, 0);
    }
    if (!unblock_port_set) {
        cps_api_object_attr_add(obj, BASE_IF_LAG_IF_INTERFACES_INTERFACE_UNBLOCK_PORT_LIST, 0, 0);
    }
}

 cps_api_return_code_t lag_object_publish(nas_lag_master_info_t *nas_lag_entry,hal_ifindex_t lag_idx,
                                cps_api_operation_types_t op)
{
    char buff[MAX_CPS_MSG_BUFF];
    memset(buff,0,sizeof(buff));
    cps_api_object_t obj_pub = cps_api_object_init(buff, sizeof(buff));
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj_pub),BASE_IF_LAG_IF_INTERFACES_INTERFACE_OBJ,cps_api_qualifier_OBSERVED);
    cps_api_set_key_data(obj_pub,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX,cps_api_object_ATTR_T_U32,
            &lag_idx,sizeof(lag_idx));
    cps_api_object_set_type_operation(cps_api_object_key(obj_pub),op);

    if(op == cps_api_oper_SET){
        nas_pack_lag_port_list(obj_pub,nas_lag_entry, DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS,false);
        cps_api_object_attr_add_u32(obj_pub,IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS,(nas_lag_entry->admin_status ?
                IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_UP : IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_DOWN) );
    }

    if (cps_api_event_thread_publish(obj_pub)!=STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE,3,"NAS-INTF-EVENT","Failed to send event.  Service issue");
        return cps_api_ret_code_ERR;
    }
    EV_LOG(INFO,INTERFACE,3,"NAS-INTF-EVENT","Published LAG object %d", lag_idx);
    return cps_api_ret_code_OK;
}


static cps_api_return_code_t nas_process_lag_block_ports(nas_lag_master_info_t  *nas_lag_entry,
        nas_port_list_t &port_index_list,bool port_state)
{
    cps_api_return_code_t rc = cps_api_ret_code_OK;

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "Processing lag block ports");

    for(auto it = port_index_list.begin() ; it != port_index_list.end() ; ++it){

        if(nas_lag_block_port(nas_lag_entry,*it,port_state) != STD_ERR_OK)
        {
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                    "Error Block/unblock Port %d",*it);
            return cps_api_ret_code_ERR;
        }
    }

    return rc;
}

static bool nas_lag_process_member_ports(cps_api_object_t obj,nas_port_list_t & list,
                                         const cps_api_object_it_t & it){
    cps_api_object_it_t it_lvl_1 = it;
    interface_ctrl_t i;
    cps_api_attr_id_t ids[3] = {DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS,0,
                                DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS_NAME };
    const int ids_len = sizeof(ids)/sizeof(ids[0]);

    for (cps_api_object_it_inside (&it_lvl_1); cps_api_object_it_valid (&it_lvl_1);
         cps_api_object_it_next (&it_lvl_1)) {

        ids[1] = cps_api_object_attr_id (it_lvl_1.attr);
        cps_api_object_attr_t intf = cps_api_object_e_get(obj,ids,ids_len);

        if(intf == NULL){
            EV_LOG(ERR,INTERFACE,0,"NAS-CPS-LAG","No Interface Name is passed");
            return false;
        }

       if(!nas_lag_get_intf_ctrl_info((const char *)cps_api_object_attr_data_bin(intf),i)){
           return false;
       }

       list.insert(i.if_index);

    }

    return true;

}

static cps_api_return_code_t nas_cps_set_lag(cps_api_object_t obj)
{
    hal_ifindex_t lag_index = 0;
    cps_api_return_code_t rc = cps_api_ret_code_OK;
    nas_lag_master_info_t *nas_lag_entry;
    cps_api_object_it_t it;
    nas_port_list_t port_list;

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "CPS Set LAG");

    if(!nas_lag_get_ifindex_from_obj(obj,&lag_index)){
        return cps_api_ret_code_ERR;
    }

    cps_api_object_attr_add_u32(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX, lag_index);

    nas_lag_entry = nas_get_lag_node(lag_index);

    if(nas_lag_entry == NULL) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "Lag node is NULL");
        return cps_api_ret_code_ERR;
    }

    cps_api_object_attr_t member_port_attr = cps_api_get_key_data(obj, DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS_NAME);

    bool port_list_attr = false;
    interface_ctrl_t i;

    if(member_port_attr != NULL){
        interface_ctrl_t _if;
        if(!nas_lag_get_intf_ctrl_info((const char *)cps_api_object_attr_data_bin(member_port_attr),_if)){
            return cps_api_ret_code_ERR;
        }
        port_list.insert(_if.if_index);
        port_list_attr = true;
    }

    cps_api_object_it_begin(obj,&it);
    for ( ; cps_api_object_it_valid(&it) ; cps_api_object_it_next(&it) ) {
        int id = (int) cps_api_object_attr_id(it.attr);
        switch (id) {
            case DELL_IF_IF_INTERFACES_INTERFACE_PHYS_ADDRESS:
                rc = nas_cps_set_mac(obj,lag_index);
                break;
            case IF_INTERFACES_INTERFACE_ENABLED:
                rc = nas_cps_set_admin_status(obj,lag_index,nas_lag_entry);
                break;
            case DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS:
                port_list_attr =true;
                if (cps_api_object_attr_len(it.attr) != 0) {
                   if(!nas_lag_process_member_ports(obj,port_list,it)){
                       return cps_api_ret_code_ERR;
                   }
                }
                break;
            case BASE_IF_LAG_IF_INTERFACES_INTERFACE_BLOCK_PORT_LIST:
                if (cps_api_object_attr_len(it.attr) != 0) {
                    if(!nas_lag_get_intf_ctrl_info((const char *)cps_api_object_attr_data_bin(it.attr),i)){
                        return cps_api_ret_code_ERR;
                    }
                    port_list.insert(i.if_index);
                }
                rc = nas_process_lag_block_ports(nas_lag_entry, port_list,true);
                break;
            case BASE_IF_LAG_IF_INTERFACES_INTERFACE_UNBLOCK_PORT_LIST:
                if (cps_api_object_attr_len(it.attr) != 0) {
                    if(!nas_lag_get_intf_ctrl_info((const char *)cps_api_object_attr_data_bin(it.attr),i)){
                        return cps_api_ret_code_ERR;
                    }
                    port_list.insert(i.if_index);
                }
                rc = nas_process_lag_block_ports(nas_lag_entry, port_list,false);
                break;
            default:
                EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
                        "Received attrib %d", id);
                break;
        }
    }

    if(port_list_attr) {
        EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
                "Received %d valid ports ", port_list.size());
        cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));
        bool create = (op == cps_api_oper_CREATE )? true : false;
        if(cps_lag_update_ports(nas_lag_entry, port_list,create) !=STD_ERR_OK)
        {
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                    "nas_process_cps_ports failure");
            return cps_api_ret_code_ERR;
        }

        op = cps_api_oper_SET;
        // publish CPS_LAG events on port add/delete
        if(lag_object_publish(nas_lag_entry,lag_index,op)!= cps_api_ret_code_OK)
        {
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                    "LAG events publish failure");
            return cps_api_ret_code_ERR;
        }
    }

    return rc;
}



static void nas_pack_lag_if(cps_api_object_t obj, nas_lag_master_info_t *nas_lag_entry)
{
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),BASE_IF_LAG_IF_INTERFACES_INTERFACE_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX,
            cps_api_object_ATTR_T_U32,
            &nas_lag_entry->ifindex,sizeof(nas_lag_entry->ifindex));
    cps_api_set_key_data(obj,IF_INTERFACES_INTERFACE_NAME,
                cps_api_object_ATTR_T_BIN,
                nas_lag_entry->name, strlen(nas_lag_entry->name)+1);

    cps_api_object_attr_add_u32(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX,nas_lag_entry->ifindex);

    cps_api_object_attr_add_u32(obj, BASE_IF_LAG_IF_INTERFACES_INTERFACE_ID, nas_lag_entry->lag_id);

    cps_api_object_attr_add(obj, IF_INTERFACES_INTERFACE_NAME, nas_lag_entry->name,
            strlen(nas_lag_entry->name)+1);

    nas_pack_lag_port_list(obj,nas_lag_entry,DELL_IF_IF_INTERFACES_INTERFACE_MEMBER_PORTS,true);

    if(!nas_lag_entry->mac_addr.empty()){
        cps_api_object_attr_add(obj,DELL_IF_IF_INTERFACES_INTERFACE_PHYS_ADDRESS, nas_lag_entry->mac_addr.c_str(),
           nas_lag_entry->mac_addr.length()+1);
    }

    cps_api_object_attr_add_u32(obj,IF_INTERFACES_INTERFACE_ENABLED ,nas_lag_entry->admin_status);

    nas::ndi_obj_id_table_t lag_opaque_data_table;
    //@TODO to retrive NPU ID in multi npu case
    lag_opaque_data_table[0] = nas_lag_entry->ndi_lag_id;
    cps_api_attr_id_t  attr_id_list[] = {BASE_IF_LAG_IF_INTERFACES_INTERFACE_LAG_OPAQUE_DATA};
    nas::ndi_obj_id_table_cps_serialize (lag_opaque_data_table, obj, attr_id_list,
            sizeof(attr_id_list)/sizeof(attr_id_list[0]));
}


t_std_error nas_get_lag_intf(hal_ifindex_t ifindex, cps_api_object_list_t list)
{
    nas_lag_master_info_t *nas_lag_entry = NULL;

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-LAG-CPS",
            "Get lag interface %d", ifindex);

    nas_lag_entry = nas_get_lag_node(ifindex);

    if(nas_lag_entry == NULL) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-LAG-CPS",
                "Error finding lagnode %d for get operation", ifindex);
        return(STD_ERR(INTERFACE, FAIL, 0));
    }
    cps_api_object_t object = cps_api_object_list_create_obj_and_append(list);

    if(object == NULL){
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "obj NULL failure");
        return(STD_ERR(INTERFACE, FAIL, 0));
    }

    nas_pack_lag_if(object, nas_lag_entry);

    return STD_ERR_OK;
}


t_std_error nas_lag_get_all_info(cps_api_object_list_t list)
{
    nas_lag_master_info_t *nas_lag_entry = NULL;
    nas_lag_master_table_t nas_lag_master_table;

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-LAG-CPS",
            "Getting all lag interfaces");

    nas_lag_master_table = nas_get_lag_table();

    for (auto it =nas_lag_master_table.begin();it != nas_lag_master_table.end();++it){

        EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-LAG-CPS",
                "Inside Loop");
        cps_api_object_t obj = cps_api_object_list_create_obj_and_append(list);
        if (obj == NULL) {
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                    "obj NULL failure");
            return STD_ERR(INTERFACE, NOMEM, 0);
        }

        nas_lag_entry = nas_get_lag_node(it->first);

        if(nas_lag_entry == NULL) {
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-LAG-CPS",
                    "Error finding lagnode %d for get operation", it->first);
            return(STD_ERR(INTERFACE, FAIL, 0));
        }
        nas_pack_lag_if(obj,nas_lag_entry);
    }

    return STD_ERR_OK;
}

t_std_error nas_lag_ndi_it_to_obj_fill(nas_obj_id_t ndi_lag_id,cps_api_object_list_t list)
{
    nas_lag_master_info_t *nas_lag_entry = NULL;
    nas_lag_master_table_t nas_lag_master_table;

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-LAG-CPS",
            "Fill opaque data....");

    if(ndi_lag_id == 0)
        return STD_ERR(INTERFACE, FAIL, 0);

    nas_lag_master_table = nas_get_lag_table();
    for (auto it =nas_lag_master_table.begin();it != nas_lag_master_table.end();++it){


        nas_lag_entry = nas_get_lag_node(it->first);

        if(nas_lag_entry == NULL) {
            EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-LAG-CPS",
                    "Error finding lag Entry %d for get operation", it->first);
            return(STD_ERR(INTERFACE, FAIL, 0));
        }

        EV_LOG(INFO,INTERFACE,ev_log_s_MINOR, "NAS-LAG-CPS","Lg id %lld and appid %lld",
                nas_lag_entry->ndi_lag_id,ndi_lag_id);

        if(nas_lag_entry->ndi_lag_id == ndi_lag_id){
            cps_api_object_t obj = cps_api_object_list_create_obj_and_append(list);
            if (obj == NULL) {
                EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                        "obj NULL failure");
                return STD_ERR(INTERFACE, NOMEM, 0);
            }
            nas_pack_lag_if(obj,nas_lag_entry);
            return STD_ERR_OK;
        }
    }

    return (STD_ERR(INTERFACE,FAIL,0));
}

static cps_api_return_code_t nas_process_cps_lag_get(void * context, cps_api_get_params_t * param,
        size_t ix) {
    hal_ifindex_t ifindex = 0;
    bool opaque_attr_data = false;
    nas_obj_id_t ndi_lag_id=0;

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "cps_nas_lag_get_function");

    cps_api_object_t obj = cps_api_object_list_get(param->filters, ix);

    cps_api_object_attr_t ndi_lag_id_attr = cps_api_object_attr_get(obj,
                    BASE_IF_LAG_IF_INTERFACES_INTERFACE_LAG_OPAQUE_DATA);

    if(ndi_lag_id_attr != nullptr){
        EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
                "LAG OPAQUE DATA FOUND %lld!!!",cps_api_object_attr_data_u64(ndi_lag_id_attr));
        ndi_lag_id = cps_api_object_attr_data_u64(ndi_lag_id_attr);
        opaque_attr_data=true;
    }

    std_mutex_simple_lock_guard lock_t(nas_lag_mutex_lock());

    if(nas_lag_get_ifindex_from_obj(obj,&ifindex)){
        if(nas_get_lag_intf(ifindex, param->list)!= STD_ERR_OK){
            return cps_api_ret_code_ERR;
        }
    }else if(opaque_attr_data == true){
        if(nas_lag_ndi_it_to_obj_fill(ndi_lag_id,param->list) != STD_ERR_OK)
            return cps_api_ret_code_ERR;
    }else{
        if(nas_lag_get_all_info(param->list) != STD_ERR_OK)
            return cps_api_ret_code_ERR;
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t nas_process_cps_lag_set(void *context, cps_api_transaction_params_t *param,
                                                      size_t ix)
{
    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));
    cps_api_return_code_t rc = cps_api_ret_code_OK;

    EV_LOG(INFO,INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "cps_nas_lag_set_function");

    cps_api_object_t cloned = cps_api_object_list_create_obj_and_append(param->prev);
    if(cloned == NULL){
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-CPS-LAG",
                "obj NULL failure");
        return cps_api_ret_code_ERR;
    }

    cps_api_object_clone(cloned,obj);

    // Acquring lock to avoid pocessing netlink msg from kernel.
    std_mutex_simple_lock_guard lock_t(nas_lag_mutex_lock());

    if( op == cps_api_oper_CREATE){
        return (nas_cps_create_lag(obj));
    }
    if(op == cps_api_oper_DELETE){
        return (nas_cps_delete_lag(obj));
    }
    if(op == cps_api_oper_SET){
        return (nas_cps_set_lag(obj));
    }

    return rc;
}

void nas_lag_port_oper_state_cb(npu_id_t npu, npu_port_t port, IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_t status)
{
    ndi_port_t ndi_port;
    ndi_port.npu_id = npu;
    ndi_port.npu_port = port;
    hal_ifindex_t slave_index, master_index;

    if (nas_int_get_if_index_from_npu_port(&slave_index, &ndi_port) != STD_ERR_OK) {
        return;
    }
    std_mutex_simple_lock_guard lock_t(nas_lag_mutex_lock());
    if ( (master_index = nas_get_master_idx(slave_index)) == -1 ) {
        return; // not a part of any lag  so nothing to do
    }
    nas_lag_master_info_t *nas_lag_entry= NULL;
    if ((nas_lag_entry = nas_get_lag_node(master_index)) == NULL ) {
        return;
    }
    if (nas_lag_block_port(nas_lag_entry, slave_index,
               (status == IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_UP) ? false : true) != STD_ERR_OK){
            EV_LOGGING(INTERFACE, ERR, "NAS-CPS-LAG",
                "Error Block/unblock Port %s lag %s ",slave_index, master_index);
        return ;
    }
}

t_std_error nas_cps_lag_init(cps_api_operation_handle_t lag_intf_handle) {

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-CPS-LAG",
            "CPS LAG Initialize");

    if (intf_obj_handler_registration(obj_INTF, nas_int_type_LAG, nas_process_cps_lag_get, nas_process_cps_lag_set) != STD_ERR_OK) {
           EV_LOG(ERR,INTERFACE, 0,"NAS-LAG-INIT", "Failed to register LAG interface CPS handler");
           return STD_ERR(INTERFACE,FAIL,0);
    }
    /*  register a handler for physical port oper state change */
    nas_int_oper_state_register_cb(nas_lag_port_oper_state_cb);
    return STD_ERR_OK;;

}

