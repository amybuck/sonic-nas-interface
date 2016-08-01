
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
 * filename: hal_interface_common.h
 */

/*!
 * \brief  Interface Mgmt Internal API
 * Internal API
 */

#ifndef __HAL_BASE_INTERFACE_INTERNAL_H
#define __HAL_BASE_INTERFACE_INTERNAL_H

#include "std_error_codes.h"
#include "ds_common_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "ietf-interfaces.h"
#include "nas_ndi_port.h"
#include "cps_api_events.h"
#include "cps_api_interface_types.h"
#include "cps_api_object.h"


/*  LINK HDR size is added in IP MTU before programming the MTU in NPU
 *  Ethernet (untagged)                    :18 bytes
 *  Vlan Tag                               :22 bytes
 *  untagged packet with vlan-stack header :22 bytes
 *  Tagged packet with vlan-stack header   :26 bytes
 *  With all 2 bytes padding  and 4 bytes extra
 */
#define NAS_LINK_MTU_HDR_SIZE 32

typedef enum {
    npu_link_state_A_NPU=1,
    npu_link_state_A_PORT=2,
    npu_link_state_A_STATE=3,
} npu_link_state_ATTR_t;


#define IF_VLAN_MAX (4096)

/**
 * Create all interfaces
 * @return standard return code
 */
t_std_error hal_create_interfaces(void);

/**
 * Send a packet on an interface
 * @param npu target switch
 * @param port valid target logical port (relative to npu)
 * @param queue the queue id to use
 * @param data the data packet to send
 * @param len length of the packet
 * @return standard return code
 */
t_std_error hal_virtual_interace_send(npu_id_t npu, npu_port_t port, int queue,
        const void * data, unsigned int len);

//! the callback that will be used to process a packet
typedef void (*hal_virt_pkt_transmit)(npu_id_t npu, npu_port_t port,
        void *data, unsigned int len);

/**
 * Wait for a packet and call the callback function based on the correct npu,port
 * @param fun callback function to call with the correct params
 * @param buff to use to hold the received data packet
 * @param len is the length of the packet
 * @return standard return code
 */
t_std_error hal_virtual_interface_wait(hal_virt_pkt_transmit fun,
        void *buff, unsigned int len);

/**
 * The callback that is used to indicate that the port state has cahnged
 * @param npu switch that has received the event
 * @param port is the port relative to the switch
 * @param data the new state
 */
void hal_interface_link_state_cb(npu_id_t npu, npu_port_t port,
        ndi_port_oper_status_t data);

/**
 * Register a debug CLI
 */
void hal_interface_link_debug_init(void);

/**
 * Send an event to the event dispatcher using the interface subsystem's connection
 * non-blocking message send
 * @param msg is the message to send
 */
void hal_interface_send_event(cps_api_object_t obj);
bool nas_int_ev_handler_cb(cps_api_object_t obj, void *param);

t_std_error hal_int_name_to_if_index(hal_ifindex_t *if_index, const char *name);

/**
 * Send an admin state change event
 * @param if_index interface index ID for the interface
 * @state Admin state
 */

void nas_send_admin_state_event(hal_ifindex_t if_index, IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_t state);

typedef void (*oper_state_handler_t) (npu_id_t npu, npu_port_t port, IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_t oper_state);

void nas_int_oper_state_register_cb(oper_state_handler_t oper_state_cb);
#ifdef __cplusplus
}
#endif


#endif
