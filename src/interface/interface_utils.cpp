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
 * interface_utils.cpp
 *
 *  Created on: Feb 1, 2016
 *      Author: cwichmann
 */

#include "cps_api_object_key.h"
#include "std_error_codes.h"

#include "cps_api_object_tools.h"
#include "cps_api_operation_tools.h"
#include "cps_api_operation.h"
#include "cps_class_map.h"

#include "dell-base-if.h"

bool interface_util_obj_get_uint32(cps_api_object_t obj,cps_api_attr_id_t id, uint32_t &val) {
    cps_api_object_attr_t __var = cps_api_get_key_data(obj,id);
    if (__var!=nullptr) {
        val = cps_api_object_attr_data_u32(__var);
        return true;
    }
    return false;
}

t_std_error interface_load_cps_object(cps_api_object_t obj, cps_api_object_list_t &lst, int retry) {
    lst = nullptr;

    cps_api_object_list_guard lg(cps_api_object_list_create());

    t_std_error rc;
    if ((rc=cps_api_get_objs(obj,lg.get(),retry,200))!=cps_api_ret_code_OK) {
        return rc;
    }
    lst = lg.get();
    lg.release();
    return STD_ERR_OK;
}

t_std_error interface_load_cps_object(cps_api_attr_id_t id, cps_api_object_list_t &lst, int retry) {
    lst=nullptr;

    cps_api_object_guard og(cps_api_object_create());

    if (og.get()==nullptr) {
        return STD_ERR(INTERFACE,FAIL,0);
    }

    if (!cps_api_key_from_attr_with_qual(cps_api_object_key(og.get()),id, cps_api_qualifier_TARGET)) {
        return STD_ERR(INTERFACE,FAIL,0);
    }

    return interface_load_cps_object(og.get(),lst,retry);
}

void * interface_util_get_attr(cps_api_object_t obj, cps_api_attr_id_t id) {
    cps_api_object_attr_t __var = cps_api_get_key_data(obj,id);
    if (__var!=nullptr) {
        return cps_api_object_attr_data_bin(__var);
    }
    return nullptr;
}

void interface_util_get_filter_details(cps_api_object_t obj, const char *&name,int &ifix) {
    cps_api_object_attr_t __filt_ix = cps_api_get_key_data(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX);

    if (__filt_ix!=nullptr) {
        ifix = (int)cps_api_object_attr_data_uint(__filt_ix);
    } else {
        ifix = -1;
    }

    cps_api_object_attr_t __filt_name = cps_api_get_key_data(obj,IF_INTERFACES_INTERFACE_NAME);
    if (__filt_name!=nullptr) {
        name = (const char*)cps_api_object_attr_data_bin(__filt_name);
    } else {
        name = nullptr;
    }
}
