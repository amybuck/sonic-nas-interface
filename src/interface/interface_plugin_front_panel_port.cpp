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

 */

#include "plugins/interface_plugin_front_panel_port.h"


#include "plugins/interface_utils.h"

#include "dell-base-phy-interface.h"
#include "dell-base-if-phy.h"
#include "dell-base-if.h"

#include "cps_api_object_tools.h"
#include "cps_api_operation_tools.h"
#include "cps_api_object_key.h"
#include "cps_class_map.h"


#include "std_assert.h"
#include "event_log.h"


t_std_error FrontPanelPortDetails::init(InterfacePluginSequencer *seq) {

    EV_LOG(INFO,INTERFACE,0,"NAS-IF-LINUXINIT","Initializing and loading linux cache.");

    cps_api_object_list_t lst;

    t_std_error rc = interface_load_cps_object(BASE_PORT_FRONT_PANEL_PORT,lst);
    if (rc!=STD_ERR_OK) return rc;

    cps_api_object_list_guard lg(lst);

    while (cps_api_object_list_size(lg.get()) > 0) {
        cps_api_object_t _old_obj = cps_api_object_list_get(lg.get(),0);

        cps_api_object_guard _old_og(_old_obj);

        cps_api_object_list_remove(lg.get(),0);

        cps_api_object_guard _obj(cps_api_obj_tool_create(cps_api_qualifier_TARGET,BASE_IF_PHY_IF_INTERFACES_INTERFACE,false));
        if (_obj.get()==nullptr) {
            return STD_ERR(INTERFACE,FAIL,0);
        }
        /* @Todo, lst is not used, commenting to avoid compilation warning. revisit later
        std::pair<int, int> lst[] = {
                { BASE_PORT_FRONT_PANEL_PORT_UNIT_ID, BASE_IF_PHY_IF_INTERFACES_INTERFACE_NPU_ID },
                { BASE_PORT_FRONT_PANEL_PORT_UNIT_ID, BASE_IF_PHY_IF_INTERFACES_INTERFACE_NPU_ID },
                { BASE_PORT_FRONT_PANEL_PORT_PORT, BASE_IF_PHY_IF_INTERFACES_STATE_INTERFACE_PORT },
                { BASE_PORT_FRONT_PANEL_PORT_FRONT_PANEL_PORT, BASE_IF_PHY_IF_INTERFACES_STATE_INTERFACE_FRONT_PANEL_PORT },
        };*/

        uint32_t val;
        if (interface_util_obj_get_uint32(_old_og.get(),BASE_PORT_FRONT_PANEL_PORT_UNIT_ID,val)) {
            cps_api_object_attr_add_u32(_obj.get(),BASE_IF_PHY_IF_INTERFACES_STATE_INTERFACE_NPU_ID,val);
        }

    }

    seq->reg(InterfacePluginSequencer::GET,InterfacePluginSequencer::LOW,this);
    seq->reg(InterfacePluginSequencer::SET,InterfacePluginSequencer::LOW,this);

    return STD_ERR_OK;
}

cps_api_return_code_t FrontPanelPortDetails::handle_set(InterfacePluginSequencer::sequencer_request_t &req) {
    return cps_api_ret_code_OK;
}

cps_api_return_code_t FrontPanelPortDetails::handle_get(InterfacePluginSequencer::sequencer_request_t &req) {
    return cps_api_ret_code_OK;
}

FrontPanelPortDetails::~FrontPanelPortDetails() {

}
