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
 * interface_cache.cpp
 *
 *  Created on: Jan 20, 2016
 *      Author: cwichmann
 */

#include "plugins/interface_object_cache.h"

#include "std_rw_lock.h"
#include "event_log.h"
#include "cps_api_object.h"

#include <unordered_map>
#include <vector>

typedef std::unordered_map<int,cps_api_object_t> _if_obj_cache_t;
typedef std::vector<_if_obj_cache_t> _complete_cache;

static std_rw_lock_t _rwlock;
static _complete_cache object_cache;

t_std_error if_obj_cache_get(if_obj_cache_types_t type,int ifindex, cps_api_object_t obj, bool merge) {
    std_rw_lock_read_guard lg(&_rwlock);

    auto it = object_cache[type].find(ifindex);
    if (it!=object_cache[type].end()) {
        bool success = false;
        if (merge) success=cps_api_object_attr_merge(obj,it->second,false);
        else success=cps_api_object_clone(obj, it->second);
        return success ? STD_ERR_OK : STD_ERR(INTERFACE,FAIL,0);
    }
    return STD_ERR(COM,FAIL,0);

}

t_std_error if_obj_cache_set(if_obj_cache_types_t type,int ifindex, cps_api_object_t obj) {
    std_rw_lock_read_guard lg(&_rwlock);

    auto it =object_cache[type].find(ifindex);
    cps_api_object_guard og(cps_api_object_create());

    if (!cps_api_object_clone(og.get(),obj)) {
        return STD_ERR(INTERFACE,FAIL,0);
    }

    if (it!=object_cache[type].end()) {
        if (it->second!=nullptr) {
            cps_api_object_delete(it->second);
            it->second = og.get();
            og.release();
        }
    } else {
        try {
            object_cache[type][ifindex] = og.get();
            og.release();
        } catch (...) {
            return STD_ERR(INTERFACE,FAIL,0);
        }
    }
    return STD_ERR_OK;
}


t_std_error if_obj_cache_init() {

    object_cache.resize(if_obj_cache_T_MAX);

    return std_rw_lock_create_default(&_rwlock);
}
