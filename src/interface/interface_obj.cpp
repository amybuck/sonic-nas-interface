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
 * interface_obj.cpp
 *
 *  Created on: Jan 19, 2016
 *      Author: cwichmann
 */

#include "plugins/interface_plugins.h"
#include "plugins/interface_plugin_linux.h"
#include "plugins/interface_plugin_front_panel_port.h"

#include "dell-base-if.h"

#include "std_error_codes.h"
#include "cps_api_operation.h"

#include "event_log.h"
#include "cps_class_map.h"


typedef cps_api_return_code_t (*rdfn) (void * context, cps_api_get_params_t * param,
        size_t key_ix);

typedef cps_api_return_code_t (*wrfn) (void * context, cps_api_transaction_params_t * param,size_t ix);


static InterfacePluginSequencer * _seq;

cps_api_return_code_t __if_interface_get(void * context, cps_api_get_params_t * param,
        size_t key_ix) {

    InterfacePluginSequencer::sequencer_request_t req;
    req._ix = key_ix;
    req._get = param;
    req._id = DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_OBJ;
    return _seq->sequence(InterfacePluginSequencer::GET,req);
}

cps_api_return_code_t __if_interface_set (void * context, cps_api_transaction_params_t * param,size_t ix) {
    return cps_api_ret_code_ERR;
    InterfacePluginSequencer::sequencer_request_t req;
    req._ix = ix;
    req._tran = param;
    req._id = DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_OBJ;
    return _seq->sequence(InterfacePluginSequencer::SET,req);
}

cps_api_return_code_t __if_interface_state_get(void * context, cps_api_get_params_t * param,
        size_t key_ix) {
    return cps_api_ret_code_ERR;
}

cps_api_return_code_t __if_interface_state_set (void * context, cps_api_transaction_params_t * param,size_t ix) {
    return cps_api_ret_code_ERR;
}

cps_api_return_code_t __if_interface_state_statistics_get(void * context, cps_api_get_params_t * param,
        size_t key_ix) {
    return cps_api_ret_code_ERR;
}

cps_api_return_code_t __if_interface_state_statistics_set (void * context, cps_api_transaction_params_t * param,size_t ix) {
    return cps_api_ret_code_ERR;
}

static t_std_error __reg_module(cps_api_operation_handle_t handle, cps_api_attr_id_t id, rdfn rd, wrfn wr ) {
    cps_api_registration_functions_t f;
    memset(&f,0,sizeof(f));

    char buff[CPS_API_KEY_STR_MAX];
    if (!cps_api_key_from_attr_with_qual(&f.key,id,cps_api_qualifier_TARGET)) {
        EV_LOG(ERR,INTERFACE,0,"NAS-IF-REG","Could not translate %d to key %s",
            (int)(id),cps_api_key_print(&f.key,buff,sizeof(buff)-1));
        return STD_ERR(INTERFACE,FAIL,0);
    }

    f.handle = handle;
    f._read_function = rd;
    f._write_function = wr;

    if (cps_api_register(&f)!=cps_api_ret_code_OK) {
        return STD_ERR(INTERFACE,FAIL,0);
    }

    return STD_ERR_OK;
}

t_std_error interface_obj_init(cps_api_operation_handle_t handle)  {

    std::vector<InterfacePluginExtn*> entns = {
            new LinuxInterfacePluginExtn
    };

    _seq = new InterfacePluginSequencer(entns);

    if (_seq->init()!=STD_ERR_OK) {
        return STD_ERR(INTERFACE,FAIL,0);
    }

    t_std_error rc;
    if ((rc=__reg_module(handle,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_OBJ,
            __if_interface_get,__if_interface_set))!=STD_ERR_OK) {
        return rc;
    }


    if ((rc=__reg_module(handle,DELL_BASE_IF_CMN_IF_INTERFACES_STATE_INTERFACE_OBJ,
            __if_interface_state_get,__if_interface_state_set))!=STD_ERR_OK) {
        return rc;
    }


    if ((rc=__reg_module(handle,DELL_BASE_IF_CMN_IF_INTERFACES_STATE_INTERFACE_STATISTICS_OBJ,
            __if_interface_state_statistics_get,__if_interface_state_statistics_set))!=STD_ERR_OK) {
        return rc;
    }

    return STD_ERR_OK;
}
