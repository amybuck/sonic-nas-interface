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
 * interface_plugin_linux.cpp
 */

#include "plugins/interface_plugin_linux.h"
#include "plugins/interface_utils.h"

#include "dell-base-if-linux.h"
#include "dell-base-if.h"


#include "cps_api_operation_tools.h"
#include "cps_api_object_key.h"
#include "cps_class_map.h"


#include "std_assert.h"
#include "event_log.h"


t_std_error LinuxInterfacePluginExtn::init(InterfacePluginSequencer *seq) {
    EV_LOG(INFO,INTERFACE,0,"NAS-IF-LINUXINIT","Initializing and loading linux cache.");

    cps_api_object_list_t lst;

    t_std_error rc = interface_load_cps_object(BASE_IF_LINUX_IF_INTERFACES_INTERFACE,lst);
    if (rc!=STD_ERR_OK) return rc;

    cps_api_object_list_guard lg(lst);

    while (cps_api_object_list_size(lg.get()) > 0) {
        cps_api_object_t obj = cps_api_object_list_get(lg.get(),0);
        cps_api_object_guard og(obj);
        cps_api_object_list_remove(lg.get(),0);


        cps_api_object_attr_t __ifix = cps_api_get_key_data(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX);
        if (__ifix==nullptr) continue;
        int ifix = (int)cps_api_object_attr_data_uint(__ifix);

        try {
            _obj_map[DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE][ifix] = obj;
            _obj_map[DELL_BASE_IF_CMN_IF_INTERFACES_STATE_INTERFACE][ifix] = cps_api_object_create();
            if (_obj_map[DELL_BASE_IF_CMN_IF_INTERFACES_STATE_INTERFACE][ifix]!=nullptr) {
                if (!cps_api_object_clone(_obj_map[DELL_BASE_IF_CMN_IF_INTERFACES_STATE_INTERFACE][ifix],obj)) {
                    cps_api_object_delete(_obj_map[DELL_BASE_IF_CMN_IF_INTERFACES_STATE_INTERFACE][ifix]);
                    _obj_map[DELL_BASE_IF_CMN_IF_INTERFACES_STATE_INTERFACE][ifix] = nullptr;
                    //@!TODO log error
                }
            }
            og.release();
        } catch (...) {
            return STD_ERR(INTERFACE,FAIL,0);
        }
    }

    seq->reg(InterfacePluginSequencer::GET,InterfacePluginSequencer::FIRST,this);
    seq->reg(InterfacePluginSequencer::SET,InterfacePluginSequencer::FIRST,this);

    return STD_ERR_OK;
}

cps_api_return_code_t LinuxInterfacePluginExtn::handle_set(InterfacePluginSequencer::sequencer_request_t &req) {

    return cps_api_ret_code_OK;
}

cps_api_return_code_t LinuxInterfacePluginExtn::handle_get(InterfacePluginSequencer::sequencer_request_t &req) {
    cps_api_object_t _filt =cps_api_object_list_get(req._get->filters,req._ix);

    int _filt_ix = -1;
    const char *_filt_name = nullptr;

    const BASE_CMN_INTERFACE_TYPE_t *_filt_type = (BASE_CMN_INTERFACE_TYPE_t*)interface_util_get_attr(_filt,
            (cps_api_attr_id_t)BASE_IF_LINUX_IF_INTERFACES_INTERFACE_DELL_TYPE);

    interface_util_get_filter_details(_filt,_filt_name,_filt_ix);

    for ( auto &it : _obj_map[req._id] ) {
        cps_api_object_t obj = (it.second);

        int obj_ix = -1;
        const char *obj_name= nullptr;
        interface_util_get_filter_details(_filt,obj_name,obj_ix);

        if (_filt_ix!=-1) {
            if (obj_ix!=_filt_ix) {
                continue;
            }
        }

        if (_filt_name!=nullptr) {
            if (obj_name==nullptr) continue;
            //allow partial string match
            if (strncmp(obj_name,_filt_name,strlen(_filt_name))!=0) continue;
        }

        const BASE_CMN_INTERFACE_TYPE_t *obj_type = (BASE_CMN_INTERFACE_TYPE_t*)interface_util_get_attr(obj,
                    BASE_IF_LINUX_IF_INTERFACES_INTERFACE_DELL_TYPE);

        if (_filt_type!=nullptr) {
            if (obj_type==nullptr) continue;
            if (*obj_type!=*_filt_type) continue;
        }
        cps_api_object_guard og(cps_api_object_create());
        if (og.get()==nullptr) {
            //@TODO log error
            return cps_api_ret_code_ERR;
        }
        if (!cps_api_object_clone(og.get(),obj)) {
            return cps_api_ret_code_ERR;
        }
        if (!cps_api_object_list_append(req._get->list,og.get())) {
            return cps_api_ret_code_ERR;
        }
        og.release();
    }

    return cps_api_ret_code_OK;
}

