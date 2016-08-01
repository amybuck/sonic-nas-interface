
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


/*!
 * \file   intf_main.c
 * \brief  Interface Mgmt main file
 */

#include "ds_common_types.h"
#include "cps_api_events.h"
#include "cps_api_operation.h"
#include "cps_api_object_key.h"
#include "cps_class_map.h"
#include "nas_int_bridge.h"
#include "nas_int_physical_cps.h"
#include "dell-base-if-linux.h"
#include "nas_int_lag_cps.h"
#include "interface_obj.h"

#include "hal_interface.h"
#include "hal_if_mapping.h"

#include "hal_interface_defaults.h"
#include "hal_interface_common.h"
#include "event_log_types.h"
#include "event_log.h"
#include "nas_ndi_port.h"
#include "std_assert.h"

#include "std_config_file.h"
#include "nas_int_vlan.h"
#include "nas_int_port.h"
#include "nas_stats.h"

#include "nas_ndi_port.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <set>

#define NUM_INT_CPS_API_THREAD 1

static cps_api_operation_handle_t nas_if_handle;


void hal_interface_send_event(cps_api_object_t obj) {
    if (cps_api_event_thread_publish(obj)!=STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE,3,"HAL-INTF-EVENT","Failed to send event.  Service issue");
    }
}

static std::set <oper_state_handler_t> oper_state_handlers;

void nas_int_oper_state_register_cb(oper_state_handler_t oper_state_cb) {
    if (oper_state_cb != NULL) {
        oper_state_handlers.insert(oper_state_cb);
    }
}

static void hw_link_state_cb(npu_id_t npu, npu_port_t port,
        ndi_intf_link_state_t *data) {

    IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_t status = ndi_to_cps_oper_type(data->oper_status);
    for (auto it = oper_state_handlers.begin(); it != oper_state_handlers.end(); ++it) {
        (*it)(npu, port, status);
    }
}
/*
 * Initialize the interface management module
 */
t_std_error hal_interface_init(void) {
    t_std_error rc;

    // register for events
    cps_api_event_reg_t reg;
    memset(&reg,0,sizeof(reg));
    const uint_t NUM_EVENTS=1;

    cps_api_key_t keys[NUM_EVENTS];

    char buff[CPS_API_KEY_STR_MAX];
    if (!cps_api_key_from_attr_with_qual(&keys[0],
                BASE_IF_LINUX_IF_INTERFACES_INTERFACE_OBJ, cps_api_qualifier_OBSERVED)) {
        EV_LOG(ERR,INTERFACE,0,"NAS-IF-REG","Could not translate %d to key %s",
            (int)(BASE_IF_LINUX_IF_INTERFACES_INTERFACE_OBJ),cps_api_key_print(&keys[0],buff,sizeof(buff)-1));
        return STD_ERR(INTERFACE,FAIL,0);
    }
    EV_LOG(INFO, INTERFACE,0,"NAS-IF-REG", "Registered for interface events with key %s",
                    cps_api_key_print(&keys[0],buff,sizeof(buff)-1));

    reg.number_of_objects = NUM_EVENTS;
    reg.objects = keys;

    if (cps_api_event_thread_reg(&reg,nas_int_ev_handler_cb,NULL)!=cps_api_ret_code_OK) {
        return STD_ERR(INTERFACE,FAIL,0);
    }

    //Create a handle for CPS objects
    if (cps_api_operation_subsystem_init(&nas_if_handle,NUM_INT_CPS_API_THREAD)!=cps_api_ret_code_OK) {
        return STD_ERR(CPSNAS,FAIL,0);
    }

    if ( (rc=interface_obj_init(nas_if_handle))!=STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE, 0,"NAS-INT-INIT-IF", "Failed to initialize common interface handler");
        return rc;
    }

    if (ndi_port_oper_state_notify_register(hw_link_state_cb)!=STD_ERR_OK) {
        EV_LOGGING(INTERFACE, ERR,"NAS-INT-INIT","Initializing Interface callback failed");
        return STD_ERR(INTERFACE,FAIL,0);
    }
    if ( (rc=nas_int_cps_init(nas_if_handle))!= STD_ERR_OK) {
        EV_LOG_ERR(ev_log_t_INTERFACE, 3,"NAS-INT-SWERR", "Initializing interface management failed");
        return rc;
    }

    if((rc = nas_cps_lag_init(nas_if_handle)) != STD_ERR_OK) {
        EV_LOG_ERR(ev_log_t_INTERFACE, 0,"NAS-INTF-CPS-LAG-SWERR", "Initializing CPS for LAG failed");
        return rc;
    }

    if((rc = nas_cps_vlan_init(nas_if_handle)) != STD_ERR_OK) {
        EV_LOG_ERR(ev_log_t_INTERFACE, 0,"NAS-INTF-CPS-VLAN-SWERR", "Initializing CPS for VLAN failed");
        return rc;
    }

    if ( (rc=nas_stats_if_init(nas_if_handle))!= STD_ERR_OK) {
        EV_LOG_ERR(ev_log_t_INTERFACE, 0,"NAS-INT-SWERR", "Initializing interface statistic failed");
        return rc;
    }

    if ( (rc=nas_stats_vlan_init(nas_if_handle))!= STD_ERR_OK) {
        EV_LOG_ERR(ev_log_t_INTERFACE, 0,"NAS-INT-SWERR", "Initializing vlan statistics failed");
        return rc;
    }


    return (rc);
}

