
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
 * filename: nas_stats_if_cps.cpp
 */



#include "dell-base-if.h"
#include "dell-interface.h"
#include "ietf-interfaces.h"
#include "interface_obj.h"
#include "hal_if_mapping.h"

#include "cps_api_key.h"
#include "cps_api_object_key.h"
#include "cps_class_map.h"
#include "cps_api_operation.h"
#include "event_log.h"
#include "nas_ndi_plat_stat.h"
#include "nas_stats.h"
#include "nas_ndi_port.h"

#include <time.h>
#include <vector>
#include <unordered_map>
#include <stdint.h>

std::vector<ndi_stat_id_t> if_stat_ids;

bool nas_stat_get_ifindex_from_obj(cps_api_object_t obj,hal_ifindex_t *index, bool clear){
    cps_api_attr_id_t attr_id;
    if(clear){
        attr_id = DELL_IF_CLEAR_COUNTERS_INPUT_INTF_CHOICE_IFNAME_CASE;
    }else{
        attr_id = IF_INTERFACES_STATE_INTERFACE_NAME;
    }
    cps_api_object_attr_t if_name_attr = cps_api_get_key_data(obj,attr_id);
    cps_api_object_attr_t if_index_attr = cps_api_object_attr_get(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX );

    if(if_index_attr == NULL && if_name_attr == NULL) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-STAT",
            "Missing Name/ifindex attribute for STAT Get");
        return false;
    }

    if(if_index_attr){
        *index = (hal_ifindex_t) cps_api_object_attr_data_u32(if_index_attr);
    }else{
        const char * name = (const char *)cps_api_object_attr_data_bin(if_name_attr);
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

static t_std_error populate_if_stat_ids(){

    unsigned int max_if_stat_id;
    if(ndi_plat_get_ids_len(NAS_STAT_IF,&max_if_stat_id ) != STD_ERR_OK){
        EV_LOG(ERR,INTERFACE, 0,"NAS-STAT", "Failed to get max length of supported stat ids");
        return STD_ERR(INTERFACE,FAIL,0);
    }

    ndi_stat_id_t ids_list[max_if_stat_id];

    memset(ids_list,0,sizeof(ids_list));
    if(ndi_plat_port_stat_list_get(ids_list, &max_if_stat_id) != STD_ERR_OK) {
        return STD_ERR(INTERFACE,FAIL,0);
    }

    for(unsigned int ix = 0 ; ix < max_if_stat_id ; ++ix ){
        if_stat_ids.push_back(ids_list[ix]);
    }

    return STD_ERR_OK;
}


static bool get_stats(hal_ifindex_t ifindex, cps_api_object_list_t list){

    cps_api_object_t obj = cps_api_object_list_create_obj_and_append(list);

    if (obj == NULL) {
        EV_LOG(ERR,INTERFACE, 0,"NAS-STAT", "Failed to create/append new object to list");
        return false;
    }

    interface_ctrl_t intf_ctrl;
    memset(&intf_ctrl, 0, sizeof(interface_ctrl_t));

    intf_ctrl.q_type = HAL_INTF_INFO_FROM_IF;
    intf_ctrl.if_index = ifindex;

    if (dn_hal_get_interface_info(&intf_ctrl) != STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE,0,"NAS-STAT","Interface %d has NO slot %d, port %d",
               intf_ctrl.if_index, intf_ctrl.npu_id, intf_ctrl.port_id);
        return false;
    }

    const size_t max_port_stat_id = if_stat_ids.size();
    uint64_t stat_values[max_port_stat_id];
    memset(stat_values,0,sizeof(stat_values));

    if(ndi_port_stats_get(intf_ctrl.npu_id, intf_ctrl.port_id,
                          (ndi_stat_id_t *)&if_stat_ids[0],
                          stat_values,max_port_stat_id) != STD_ERR_OK) {
        return false;
    }

    for(unsigned int ix = 0 ; ix < max_port_stat_id ; ++ix ){
        cps_api_object_attr_add_u64(obj, if_stat_ids[ix], stat_values[ix]);
    }

    cps_api_object_attr_add_u32(obj,DELL_BASE_IF_CMN_IF_INTERFACES_STATE_INTERFACE_STATISTICS_TIME_STAMP,time(NULL));

    return true;
}


static cps_api_return_code_t if_stats_get (void * context, cps_api_get_params_t * param,
                                           size_t ix) {

    cps_api_object_t obj = cps_api_object_list_get(param->filters,ix);

    hal_ifindex_t ifindex=0;
    if(!nas_stat_get_ifindex_from_obj(obj,&ifindex,false)){
        return (cps_api_return_code_t)STD_ERR(INTERFACE,CFG,0);
    }


    if(get_stats(ifindex,param->list)) return cps_api_ret_code_OK;

    return (cps_api_return_code_t)STD_ERR(INTERFACE,FAIL,0);
}

static cps_api_return_code_t if_stats_set (void * context, cps_api_transaction_params_t * param,
                                           size_t ix) {
   return cps_api_ret_code_ERR;
}


static cps_api_return_code_t if_stats_clear (void * context, cps_api_transaction_params_t * param,
                                             size_t ix) {

    cps_api_object_t obj = cps_api_object_list_get(param->change_list,ix);
    cps_api_operation_types_t op = cps_api_object_type_operation(cps_api_object_key(obj));

    if (op != cps_api_oper_ACTION) {
        EV_LOG(ERR,INTERFACE,0,"NAS-STAT","Invalid operation %d for clearing stat",op);
        return (cps_api_return_code_t)STD_ERR(INTERFACE,PARAM,0);
    }

    hal_ifindex_t ifindex=0;
    if(!nas_stat_get_ifindex_from_obj(obj,&ifindex,true)){
        return (cps_api_return_code_t)STD_ERR(INTERFACE,CFG,0);
    }


    interface_ctrl_t intf_ctrl;
    memset(&intf_ctrl, 0, sizeof(interface_ctrl_t));

    intf_ctrl.q_type = HAL_INTF_INFO_FROM_IF;
    intf_ctrl.if_index = ifindex;

    if (dn_hal_get_interface_info(&intf_ctrl) != STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE,0,"NAS-STAT","Interface %d has NO slot %d, port %d",
               intf_ctrl.if_index, intf_ctrl.npu_id, intf_ctrl.port_id);
        return (cps_api_return_code_t)STD_ERR(INTERFACE,FAIL,0);
    }

    if(ndi_port_clear_all_stat(intf_ctrl.npu_id,intf_ctrl.port_id) != STD_ERR_OK) {
        return (cps_api_return_code_t)STD_ERR(INTERFACE,FAIL,0);
    }

    return cps_api_ret_code_OK;
}


t_std_error nas_stats_if_init(cps_api_operation_handle_t handle) {

    if (intf_obj_handler_registration(obj_INTF_STATISTICS, nas_int_type_PORT,
                                      if_stats_get, if_stats_set) != STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE, 0,"NAS-LAG-INIT", "Failed to register interface stats CPS handler");
        return STD_ERR(INTERFACE,FAIL,0);
    }

    cps_api_registration_functions_t f;
    memset(&f,0,sizeof(f));

    char buff[CPS_API_KEY_STR_MAX];
    memset(buff,0,sizeof(buff));


    if (!cps_api_key_from_attr_with_qual(&f.key,DELL_IF_CLEAR_COUNTERS_OBJ,
                                         cps_api_qualifier_TARGET)) {
        EV_LOG(ERR,INTERFACE,0,"NAS-STATS","Could not translate %d to key %s",
               (int)(DELL_IF_CLEAR_COUNTERS_OBJ),cps_api_key_print(&f.key,buff,sizeof(buff)-1));
        return STD_ERR(INTERFACE,FAIL,0);
    }

    f.handle = handle;
    f._write_function = if_stats_clear;

    if (cps_api_register(&f)!=cps_api_ret_code_OK) {
        return STD_ERR(INTERFACE,FAIL,0);
    }

    if (populate_if_stat_ids() != STD_ERR_OK){
        return STD_ERR(INTERFACE,FAIL,0);
    }

    return STD_ERR_OK;
}
