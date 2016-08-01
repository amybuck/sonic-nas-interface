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
 * interface_plugins.cpp
 *
 *  Created on: Jan 31, 2016
 */

#include "std_assert.h"
#include "plugins/interface_plugins.h"


t_std_error InterfacePluginSequencer::init() {
    t_std_error rc;
    for ( auto &it : _regs ) {
        rc = it->init(this);
        if (rc!=STD_ERR_OK) return rc;
    }
    return STD_ERR_OK;
}

InterfacePluginSequencer::InterfacePluginSequencer(const std::vector<InterfacePluginExtn*> &l) : _regs(l) {
    STD_ASSERT(std_rw_lock_create_default(&_lock)==STD_ERR_OK);
}

bool InterfacePluginSequencer::reg(operation_t when, orders_t where_to_insert, InterfacePluginExtn *what_to_insert) {
    STD_ASSERT(when==GET || when==SET);
    STD_ASSERT( (where_to_insert >= InterfacePluginSequencer::FIRST) || (where_to_insert <= InterfacePluginSequencer::LOW) );
    STD_ASSERT(what_to_insert!=nullptr);

    std_rw_lock_write_guard lg(&_lock);

    try {
        _op_map[when][where_to_insert].push_back(what_to_insert);
    } catch (...) {
        return false;
    }

    return true;
}

t_std_error InterfacePluginSequencer::sequence(operation_t oper, sequencer_request_t &req) {
    STD_ASSERT(oper==GET || oper==SET);

    std_rw_lock_read_guard lg(&_lock);
    t_std_error rc = STD_ERR_OK;
    size_t ix = FIRST;
    size_t mx = LOW;
    for ( ; ix < mx  ; ++ix ) {
        for (auto &it : _op_map[oper][ix] ) {
            if (oper==GET) rc = it->handle_get(req);
            else rc = it->handle_set(req);
            if (rc!=STD_ERR_OK) break;
        }

    }
    return rc;
}

InterfacePluginExtn::InterfacePluginExtn() {
    std_mutex_lock_init_recursive (&_lock);
}
