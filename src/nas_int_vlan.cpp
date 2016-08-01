
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
 * filename: nas_int_vlan.c
 */


#include "nas_ndi_vlan.h"
#include "nas_int_bridge.h"
#include "nas_int_vlan.h"
#include "event_log.h"
#include "event_log_types.h"
#include "nas_int_utils.h"
#include "nas_ndi_port.h"
#include "dell-base-if-vlan.h"
#include "dell-base-if.h"
#include "dell-interface.h"
#include "iana-if-type.h"
#include "nas_ndi_switch.h"
#include "nas_os_vlan.h"
#include "cps_api_object_key.h"
#include "cps_api_events.h"
#include "cps_class_map.h"
#include "nas_switch.h"
#include "std_mac_utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>



const static int MAX_CPS_MSG_BUFF=10000;

typedef struct br_mac {
    char  mac_str[MAC_STRING_SZ];
    bool mac_set;
} bridge_mac_t;

static bridge_mac_t bridge_mac = {{0}, false};

cps_api_return_code_t nas_publish_vlan_port_list(nas_bridge_t *p_bridge_node, nas_port_list_t &port_list,
                                                 nas_port_mode_t port_mode, cps_api_operation_types_t op)
{
    char buff[MAX_CPS_MSG_BUFF];
    memset(buff,0,sizeof(buff));
    int id = 0;
    cps_api_object_t obj_pub = cps_api_object_init(buff, sizeof(buff));

    EV_LOG(INFO,INTERFACE,3,"NAS-VLAN","VLAN publish event oper %d for VLAN %d",
           op, p_bridge_node->vlan_id);

    if(port_mode == NAS_PORT_TAGGED) {
        id = DELL_IF_IF_INTERFACES_INTERFACE_TAGGED_PORTS;
    }
    else {
        id = DELL_IF_IF_INTERFACES_INTERFACE_UNTAGGED_PORTS;
    }

    cps_api_key_from_attr_with_qual(cps_api_object_key(obj_pub), id,
                                    cps_api_qualifier_OBSERVED);

    cps_api_set_key_data(obj_pub,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX, cps_api_object_ATTR_T_U32,
                         &p_bridge_node->ifindex, sizeof(p_bridge_node->ifindex));

    cps_api_object_set_type_operation(cps_api_object_key(obj_pub),op);

    for ( auto it = port_list.begin(); it != port_list.end();++it) {
        cps_api_object_attr_add_u32(obj_pub, id, *it);
    }

    cps_api_object_attr_add_u32(obj_pub, BASE_IF_VLAN_IF_INTERFACES_INTERFACE_ID, p_bridge_node->vlan_id);

    if (cps_api_event_thread_publish(obj_pub)!=STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE,3,"NAS-VLAN","Failed to send VLAN publish event.  Service issue");
        return cps_api_ret_code_ERR;
    }
    return cps_api_ret_code_OK;
}

t_std_error nas_vlan_create(npu_id_t npu_id, hal_vlan_id_t vlan_id)
{
    EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                "Creating VLAN %d in NPU %d", vlan_id, npu_id);

    return ndi_create_vlan(npu_id, vlan_id);
}


void nas_handle_bridge_mac(nas_bridge_t *b_node, hal_ifindex_t index)
{
    char buff[MAX_CPS_MSG_BUFF];
    hal_mac_addr_t mac_addr;

    if (!bridge_mac.mac_set) {
        nas_switch_wait_for_sys_base_mac(&mac_addr);
        bridge_mac.mac_set = true;
        std_mac_to_string((const hal_mac_addr_t *)&mac_addr, bridge_mac.mac_str, sizeof(bridge_mac.mac_str));
    }
    cps_api_object_t name_obj = cps_api_object_init(buff, sizeof(buff));
    cps_api_set_key_data(name_obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX,cps_api_object_ATTR_T_U32,
            &index,sizeof(index));
    cps_api_object_attr_add(name_obj,DELL_IF_IF_INTERFACES_INTERFACE_PHYS_ADDRESS,
            bridge_mac.mac_str,strlen(bridge_mac.mac_str)+1);
    nas_cps_set_vlan_mac(name_obj, b_node);

}

t_std_error nas_add_or_del_port_to_vlan(npu_id_t npu_id, hal_vlan_id_t vlan_id,
                                        ndi_port_t *p_ndi_port, nas_port_mode_t port_mode,
                                        bool add_port)
{
    ndi_port_list_t ndi_port_list;
    ndi_port_list_t *untag_list = NULL;
    ndi_port_list_t *tag_list = NULL;

    ndi_port_list.port_count = 1;
    ndi_port_list.port_list =p_ndi_port;
    t_std_error rc = STD_ERR_OK;

    if (port_mode == NAS_PORT_TAGGED) {
        tag_list = &ndi_port_list;
    }
    else {
        untag_list = &ndi_port_list;
    }
    EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                "Updating %d port <%d %d> in VLAN %d", add_port, p_ndi_port->npu_id,
                p_ndi_port->npu_port, vlan_id);

    if (ndi_add_or_del_ports_to_vlan(npu_id, vlan_id, tag_list, untag_list, add_port)
            != STD_ERR_OK) {
        return (STD_ERR(INTERFACE,FAIL, 0));
    }

    if ((port_mode == NAS_PORT_UNTAGGED) && add_port) {
        if ((rc = ndi_set_port_vid(npu_id, p_ndi_port->npu_port, vlan_id)) != STD_ERR_OK) {
             EV_LOG(ERR, INTERFACE,ev_log_s_MINOR, "NAS-Port",
                    "Error setting untagged port <%d %d> VID",
                    npu_id, p_ndi_port->npu_port);
        }
    }

    return STD_ERR_OK;

}

t_std_error nas_vlan_delete(npu_id_t npu_id, hal_vlan_id_t vlan_id)
{
    return (ndi_delete_vlan(npu_id, vlan_id));
}

static t_std_error nas_add_port_list_to_vlan(npu_id_t npu_id, hal_vlan_id_t vlan_id,
                                               ndi_port_list_t *p_ndi_port_list,
                                               bool tagged_port_list)
{
    t_std_error rc = STD_ERR_OK;
    if (tagged_port_list) {
        if ((rc = ndi_add_or_del_ports_to_vlan(npu_id, vlan_id,  p_ndi_port_list, NULL, true))
                != STD_ERR_OK)
            return rc;
    }
    else {
        if ((rc = ndi_add_or_del_ports_to_vlan(npu_id, vlan_id, NULL, p_ndi_port_list, true))
             != STD_ERR_OK)
            return rc;
    }
    return rc;
}

//@TODO change this to the vector..
static void nas_copy_bridge_ports_to_ndi_port_list(std_dll_head *p_port_list,
                                                   ndi_port_list_t *p_ndi_ports)
{
    nas_list_node_t *p_link_iter_node = NULL, *p_temp_node = NULL;
    ndi_port_t *p_port_t = NULL;
    int count = 0;

    if ((p_port_list == NULL) ||
        (p_ndi_ports == NULL)) {
        EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                   "Bridge port list or NDI port list is empty");
        return;
    }

    p_link_iter_node = nas_get_first_link_node(p_port_list);

    while (p_link_iter_node != NULL)
    {
        EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                    "Copying untagged port %d",
                     p_link_iter_node->ndi_port.npu_port);

        p_port_t = &(p_ndi_ports->port_list[count++]);
        p_port_t->npu_id = p_link_iter_node->ndi_port.npu_id;
        p_port_t->npu_port = p_link_iter_node->ndi_port.npu_port;

        p_temp_node = p_link_iter_node;
        p_link_iter_node = nas_get_next_link_node(p_port_list, p_temp_node);
    }
}


static t_std_error nas_add_all_ut_ports_to_vlan(nas_bridge_t *p_bridge_node)
{
    ndi_port_list_t ndi_port_list;
    size_t port_count = p_bridge_node->untagged_list.port_count;
    int npu_id = 0;
    nas_list_node_t *p_iter_node = NULL;

    t_std_error err = STD_ERR_OK;

    if (port_count != 0) {
        do {
            ndi_port_list.port_list = (ndi_port_t *)malloc(sizeof (ndi_port_t) * port_count);
            if (ndi_port_list.port_list==NULL) { err = STD_ERR(INTERFACE,FAIL,0) ; break; }

            ndi_port_list.port_count = port_count;
            nas_copy_bridge_ports_to_ndi_port_list(&p_bridge_node->untagged_list.port_list,
                                                   &ndi_port_list);

            /* @todo : NPU_ID for bridge */
            if (nas_add_port_list_to_vlan(npu_id, p_bridge_node->vlan_id,
                                      &ndi_port_list,
                                      false) != STD_ERR_OK) {
                err = (STD_ERR(INTERFACE,FAIL, 0));
                break;
            }

            p_iter_node = nas_get_first_link_node(&p_bridge_node->untagged_list.port_list);

            while (p_iter_node != NULL)
            {
                ndi_set_port_vid(p_iter_node->ndi_port.npu_id,
                                 p_iter_node->ndi_port.npu_port, p_bridge_node->vlan_id);
                p_iter_node = nas_get_next_link_node(&p_bridge_node->untagged_list.port_list,
                                                     p_iter_node);
            }

        } while(0);
        free(ndi_port_list.port_list);
    }
    return err;
}

static t_std_error nas_add_all_ut_lags_to_vlan(nas_bridge_t *p_bridge_node)
{
    nas_list_node_t *p_iter_node = NULL;
    t_std_error rc = STD_ERR_OK;

    EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                "Checking untagged bond list in bridge %d", p_bridge_node->ifindex);

    p_iter_node = nas_get_first_link_node(&p_bridge_node->untagged_lag.port_list);

    while (p_iter_node != NULL)
    {
        EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                    "Found untagged bond %d in bridge %d", p_iter_node->ifindex,
                    p_bridge_node->ifindex);

        if((rc = nas_handle_lag_update_for_vlan(p_bridge_node, p_iter_node->ifindex,
                                                p_bridge_node->vlan_id, NAS_PORT_UNTAGGED,
                                                true, false)) != STD_ERR_OK) {
            rc = STD_ERR(INTERFACE,FAIL, rc);
        }
        p_iter_node = nas_get_next_link_node(&p_bridge_node->untagged_lag.port_list,
                                             p_iter_node);
    }
    return rc;
}

t_std_error nas_process_lag_for_vlan_del(nas_list_t *p_list,
                                              hal_ifindex_t if_index)
{
    nas_list_node_t *p_link_node = NULL;

    EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                "Get untagged lag interface %d ",
                 if_index);

    p_link_node = nas_get_link_node(&p_list->port_list, if_index);
    if (p_link_node) {
        EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                    "Found untagged lag interface %d for deletion",
                     if_index);

        nas_delete_link_node(&p_list->port_list, p_link_node);
        p_list->port_count--;
    }
    return STD_ERR_OK;
}
t_std_error nas_process_list_for_vlan_del(nas_bridge_t *p_bridge,
                                          nas_list_t *p_list,
                                          hal_ifindex_t if_index,
                                          hal_vlan_id_t vlan_id,
                                          nas_port_mode_t port_mode)
{
    nas_list_node_t *p_link_node = NULL;
    t_std_error rc = STD_ERR_OK;

    p_link_node = nas_get_link_node(&p_list->port_list, if_index);
    if (p_link_node) {
        EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                    "Found vlan Interface %d maps to slot %d, port %d",
                     if_index, p_link_node->ndi_port.npu_id,
                     p_link_node->ndi_port.npu_port);

        if (nas_add_or_del_port_to_vlan(p_link_node->ndi_port.npu_id, vlan_id,
                                        &(p_link_node->ndi_port), port_mode, false) != STD_ERR_OK) {
            EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                        "Error deleting port %d from vlan %d",
                         if_index, vlan_id);
            return (STD_ERR(INTERFACE,FAIL, 0));
        }

        nas_delete_link_node(&p_list->port_list, p_link_node);
        p_list->port_count--;
    }
    return rc;
}

/*
 *  Following actions are taken here
 *  1. check if port is present in the bridge memeber list
 *  2. delete from NPU
 *  3. update local DB
 *  4. publish deletion of the member
 **/
void nas_process_del_vlan_mem_from_os (hal_ifindex_t bridge_id, nas_port_list_t &port_list,
                                             nas_port_mode_t port_mode)
{
    nas_bridge_t *p_bridge_node = NULL;
    nas_int_type_t intf_type;
    nas_list_t *p_list = NULL;
    hal_ifindex_t if_index = 0;
    nas_port_list_t publish_list;


    if (port_list.empty()) {
        return;
    }
    nas_bridge_lock();
    if ((p_bridge_node = nas_get_bridge_node(bridge_id)) == NULL) {
        EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                "Error finding bridge Interface %d\n", bridge_id);
        nas_bridge_unlock();
        return;
    }
    for (auto it=port_list.begin();it != port_list.end(); ++it) {

        if_index = *it;
        p_list = (port_mode == NAS_PORT_UNTAGGED) ?
                &p_bridge_node->untagged_list : &p_bridge_node->tagged_list;

        if (nas_get_int_type(if_index, &intf_type) != STD_ERR_OK) {
            EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                       "Error finding index %d in intf_ctrl", if_index);
            break;
        }

        if (intf_type == nas_int_type_PORT) {
            EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                        "Delete Port %d from bridge %d ", if_index, bridge_id);

            //check the untagged list first
            if (nas_process_list_for_vlan_del(p_bridge_node, p_list,
                                             if_index, p_bridge_node->vlan_id,
                                             port_mode) != STD_ERR_OK) {
                break;
            }

            publish_list.insert(if_index); // TODO combine with LAG members
        } else if (intf_type == nas_int_type_LAG) {
            EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                        "Delete LAG %d from Bridge %d ", if_index, bridge_id);

            nas_base_handle_lag_del(p_bridge_node->ifindex, if_index, p_bridge_node->vlan_id);
            nas_process_lag_for_vlan_del(&p_bridge_node->untagged_lag, if_index);
        } else {
            EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                    "Unknown interface type or not supported: if_index %d type %d ", if_index, intf_type);
            break;
        }
    }

    /*  publish the member update */
    nas_publish_vlan_port_list(p_bridge_node, publish_list, port_mode, cps_api_oper_DELETE);
    nas_bridge_unlock();
    return;
}

nas_list_node_t *nas_create_vlan_port_node(nas_bridge_t *p_bridge_node,
                                      hal_ifindex_t ifindex,
                                      nas_port_mode_t port_mode,
                                      bool *create_flag) {
    nas_list_node_t *p_link_node = NULL;
    ndi_port_t ndi_port;
    t_std_error rc = STD_ERR_OK;

    EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                "Insert member %d mode %d in bridge %d",
                ifindex, port_mode, p_bridge_node->ifindex);
    if( port_mode == NAS_PORT_TAGGED) {
        p_link_node = nas_get_link_node(&p_bridge_node->tagged_list.port_list, ifindex);
    } else {
        p_link_node = nas_get_link_node(&p_bridge_node->untagged_list.port_list, ifindex);
    }

    if (p_link_node == NULL) {
        *create_flag = true;
        if((rc = (nas_int_get_npu_port(ifindex, &ndi_port))) != STD_ERR_OK) {
            EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                       "Interface %d returned error %d ",
                        ifindex, rc);
            return NULL;
        }

        p_link_node = (nas_list_node_t *)malloc(sizeof(nas_list_node_t));
        if (p_link_node != NULL) {
            memset(p_link_node, 0, sizeof(nas_list_node_t));
            p_link_node->ifindex = ifindex;
            p_link_node->ndi_port.npu_port = ndi_port.npu_port;
            p_link_node->ndi_port.npu_id = ndi_port.npu_id;

            if (port_mode == NAS_PORT_TAGGED) {
                p_bridge_node->tagged_list.port_count++;
                nas_insert_link_node(&p_bridge_node->tagged_list.port_list, p_link_node);
            }
            else {
                nas_insert_link_node(&p_bridge_node->untagged_list.port_list, p_link_node);
                p_bridge_node->untagged_list.port_count++;
            }
            EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                       "Success adding vlan ifindex %d in bridge %d",
                        ifindex, p_bridge_node->ifindex);
        }
        else
            EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                       "Vlan member insertion %d failure", ifindex);
    }
    else {
        *create_flag = false;
    }
    return p_link_node;
}

t_std_error nas_create_untagged_lag_node(nas_bridge_t *p_bridge_node,
                                         hal_ifindex_t ifindex)
{
    nas_list_node_t *p_link_node = NULL;

    p_link_node = (nas_list_node_t *)malloc(sizeof(nas_list_node_t));
    if (p_link_node != NULL) {
        memset(p_link_node, 0, sizeof(nas_list_node_t));
        p_link_node->ifindex = ifindex;

        p_bridge_node->untagged_lag.port_count++;
        nas_insert_link_node(&p_bridge_node->untagged_lag.port_list, p_link_node);
    }
    else {
        return STD_ERR(INTERFACE, FAIL, 0);
    }
    return STD_ERR_OK;
}

t_std_error nas_process_member_addition_to_vlan(nas_bridge_t *p_bridge_node, hal_ifindex_t port_idx,
                                                nas_int_type_t intf_type, nas_port_mode_t port_mode,
                                                hal_vlan_id_t vlan_id)
{
    nas_list_node_t *p_link_node = NULL;
    t_std_error rc = STD_ERR_OK;
    bool create_flag=false;

    if(intf_type == nas_int_type_PORT) {
        p_link_node = nas_create_vlan_port_node(p_bridge_node, port_idx,
                                       port_mode, &create_flag);
        if(p_link_node == NULL) {
             rc = STD_ERR(INTERFACE,FAIL, 0);
             return rc;
        }
        if(create_flag == false) {
            EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                        "Port already exists %d in Bridge %d", port_idx, p_bridge_node->ifindex);

            return rc;
        }
    }
    else {
        /* Storing the untagged bonds until the vlan id gets created */
        if((p_bridge_node->vlan_id == 0) && (vlan_id == 0)) {
            EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                        "Creating untagged bond %d in bridge %d", port_idx, p_bridge_node->ifindex);

            if(nas_create_untagged_lag_node(p_bridge_node, port_idx) != STD_ERR_OK)
                return STD_ERR(INTERFACE, FAIL, 0);
        }
    }

    if ((p_bridge_node->vlan_id == 0) && (vlan_id != 0)) {

        p_bridge_node->vlan_id = vlan_id;
        //create this vlan
        if ((rc = nas_vlan_create(0, vlan_id)) != STD_ERR_OK) {
            rc = (STD_ERR(INTERFACE,FAIL, rc));
            return rc;
        }
        /* Register this VLAN interface */
        if(nas_register_vlan_intf(p_bridge_node, HAL_INTF_OP_REG) != STD_ERR_OK) {
            rc = STD_ERR(INTERFACE, FAIL, 0);
            return rc;
        }

        if(intf_type == nas_int_type_PORT) {
            //add tagged port to vlan
            if ((rc = nas_add_or_del_port_to_vlan(p_link_node->ndi_port.npu_id, vlan_id,
                                                  &(p_link_node->ndi_port), port_mode, true)) != STD_ERR_OK) {
                rc = (STD_ERR(INTERFACE,FAIL, rc));
                return rc;
            }
        }
        else if(intf_type == nas_int_type_LAG){
            if((rc = nas_handle_lag_update_for_vlan(p_bridge_node, port_idx, vlan_id,
                                       port_mode, true, false)) != STD_ERR_OK) {
                rc= (STD_ERR(INTERFACE, FAIL,rc));
            }
        }
        //vlan just got created, add all untagged stored ports
        if((rc = nas_add_all_ut_ports_to_vlan(p_bridge_node))!= STD_ERR_OK) {
            rc = (STD_ERR(INTERFACE, FAIL, rc));
        }
        if((rc = nas_add_all_ut_lags_to_vlan(p_bridge_node)) != STD_ERR_OK) {
            rc = (STD_ERR(INTERFACE, FAIL, rc));
        }

    }
    else if(p_bridge_node->vlan_id != 0) { //handle both tagged and untagged vlan
        if(intf_type == nas_int_type_PORT) {
            if ((rc = nas_add_or_del_port_to_vlan(p_link_node->ndi_port.npu_id, p_bridge_node->vlan_id,
                                                  &(p_link_node->ndi_port), port_mode, true)) != STD_ERR_OK) {
                rc = STD_ERR(INTERFACE, FAIL, rc);
            }
        }
        else if(intf_type == nas_int_type_LAG){
            if((rc = nas_handle_lag_update_for_vlan(p_bridge_node, port_idx, p_bridge_node->vlan_id,
                                       port_mode, true, false)) != STD_ERR_OK) {
                rc = STD_ERR(INTERFACE,FAIL, rc);
            }
        }
    }
    return rc;
}

void nas_process_add_vlan_mem_from_os(hal_ifindex_t bridge_id, nas_port_list_t &port_list,
                                         hal_vlan_id_t vlan_id, nas_port_mode_t port_mode)
{
    nas_bridge_t *p_bridge_node = NULL;
    nas_int_type_t intf_type;
    nas_port_list_t publish_list;
    hal_ifindex_t if_index;

    if (port_list.empty()) {
         return;
    }
    EV_LOG_INFO(ev_log_t_INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
                "Add %s ports to vlan %d bridge ifindex %d",
                (port_mode == NAS_PORT_UNTAGGED) ? "untagged" : "tagged", vlan_id, bridge_id);
    nas_bridge_lock();
    if ((p_bridge_node = nas_get_bridge_node(bridge_id)) == NULL ) {
        EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                   "Error finding bridge %d ", bridge_id);
        nas_bridge_unlock();
    }
    for (auto it=port_list.begin();it != port_list.end(); ++it) {
        if_index = *it;
        do {
            if ((vlan_id != 0) && (p_bridge_node->vlan_id !=0)) {
                // first check if this vlan-id is configured in any other bridge.
                if (nas_find_vlan_if_exist(vlan_id, p_bridge_node) == true) {
                    EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                               "Vlan ID %d exists in another bridge", vlan_id);
                    break;
                }

                //check mismatch vlan case
                if (p_bridge_node->vlan_id != vlan_id ) {
                    EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                               "Invalid Vlan ID %d for bridge %d ", vlan_id, bridge_id);
                    break;
                }
            }

            if(nas_get_int_type(if_index, &intf_type) != STD_ERR_OK) {
                EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                           "Error finding index %d in intf_ctrl ", if_index);
                break;
            }
            if((nas_process_member_addition_to_vlan(p_bridge_node, if_index,
                                                    intf_type, port_mode, vlan_id))
                    != STD_ERR_OK) {
                EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                           "Error in adding port %d in the bridge %d ", if_index, bridge_id);
                break;
            }
            publish_list.insert(if_index);
        }while(0);
    }
    nas_publish_vlan_port_list(p_bridge_node, publish_list, port_mode, cps_api_oper_CREATE);
    nas_bridge_unlock();
    return;
}


void nas_pack_vlan_port_list(cps_api_object_t obj, nas_list_t *p_list, int attr_id)
{
    nas_list_node_t *p_link_node = NULL;
    char name[HAL_IF_NAME_SZ] = "\0";

    p_link_node = nas_get_first_link_node(&p_list->port_list);

    while(p_link_node != NULL) {
        memset(name,0,sizeof(name));
        if (nas_int_get_if_index_to_name(p_link_node->ifindex, name, sizeof(name)) == STD_ERR_OK) {
            cps_api_object_attr_add(obj, attr_id, (const void *)name, strlen(name)+1);
        }
        p_link_node = nas_get_next_link_node(&p_list->port_list, p_link_node);
    }
}

void nas_pack_vlan_if(cps_api_object_t obj, nas_bridge_t *p_bridge)
{
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj),DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_OBJ,
            cps_api_qualifier_TARGET);

    cps_api_set_key_data(obj,IF_INTERFACES_INTERFACE_NAME, cps_api_object_ATTR_T_BIN,
                         &p_bridge->name, strlen(p_bridge->name)+1);

    cps_api_object_attr_add_u32(obj,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX, p_bridge->ifindex);

    cps_api_object_attr_add_u32(obj, BASE_IF_VLAN_IF_INTERFACES_INTERFACE_ID, p_bridge->vlan_id);

    nas_pack_vlan_port_list(obj, &p_bridge->tagged_list, DELL_IF_IF_INTERFACES_INTERFACE_TAGGED_PORTS);

    nas_pack_vlan_port_list(obj, &p_bridge->untagged_list,DELL_IF_IF_INTERFACES_INTERFACE_UNTAGGED_PORTS);

    cps_api_object_attr_add(obj,DELL_IF_IF_INTERFACES_INTERFACE_PHYS_ADDRESS, p_bridge->mac_addr,
                            sizeof(p_bridge->mac_addr));

    cps_api_object_attr_add_u32(obj, IF_INTERFACES_INTERFACE_ENABLED,
            (p_bridge->admin_status == IF_INTERFACES_STATE_INTERFACE_ADMIN_STATUS_UP) ? true: false);

    bool learning_mode = p_bridge->learning_disable ? false : true;

    cps_api_object_attr_add_u32(obj, DELL_IF_IF_INTERFACES_INTERFACE_LEARNING_MODE, learning_mode);

}


t_std_error nas_get_vlan_intf(const char *if_name, cps_api_object_list_t list)
{
    nas_bridge_t *p_bridge = NULL;

    EV_LOG(INFO, INTERFACE, ev_log_s_MINOR, "NAS-Vlan",
           "Get vlan interface %s", if_name);

    p_bridge = nas_get_bridge_node_from_name(if_name);

    if(p_bridge == NULL) {
        EV_LOG(ERR, INTERFACE, ev_log_s_CRITICAL, "NAS-Vlan",
                               "Error finding bridge %s for get operation", if_name);
        return(STD_ERR(INTERFACE, FAIL, 0));
    }
    cps_api_object_t object = cps_api_object_list_create_obj_and_append(list);
    nas_pack_vlan_if(object, p_bridge);
    return STD_ERR_OK;
}


t_std_error nas_register_vlan_intf(nas_bridge_t *p_bridge, hal_intf_reg_op_type_t op)
{
    interface_ctrl_t details;

    EV_LOG(INFO,INTERFACE,0,"NAS-Vlan", "Registering VLAN %d %d with ifCntrl",
           p_bridge->ifindex, p_bridge->vlan_id);

    memset(&details,0,sizeof(details));
    details.q_type = HAL_INTF_INFO_FROM_IF;
    details.if_index = p_bridge->ifindex;
    details.vlan_id = p_bridge->vlan_id;
    details.int_type = nas_int_type_VLAN;
    strncpy(details.if_name, p_bridge->name, sizeof(details.if_name)-1);

    if (dn_hal_if_register(op, &details)!=STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE,0,"NAS-Vlan", "VLAN %d %d Not registered with ifCntrl ",
               p_bridge->ifindex, p_bridge->vlan_id);
        return STD_ERR(INTERFACE,FAIL,0);
    }
    return STD_ERR_OK;
}

cps_api_return_code_t nas_publish_vlan_object(nas_bridge_t *p_bridge_node, cps_api_operation_types_t op)
{
    char buff[MAX_CPS_MSG_BUFF];
    memset(buff,0,sizeof(buff));

    cps_api_object_t obj_pub = cps_api_object_init(buff, sizeof(buff));
    cps_api_key_from_attr_with_qual(cps_api_object_key(obj_pub), BASE_IF_VLAN_IF_INTERFACES_INTERFACE_OBJ,
                                    cps_api_qualifier_OBSERVED);

    cps_api_set_key_data(obj_pub,IF_INTERFACES_INTERFACE_NAME, cps_api_object_ATTR_T_BIN,
                         &p_bridge_node->name, strlen(p_bridge_node->name)+1);

    cps_api_object_attr_add_u32(obj_pub,DELL_BASE_IF_CMN_IF_INTERFACES_INTERFACE_IF_INDEX, p_bridge_node->ifindex);

    cps_api_object_attr_add_u32(obj_pub, BASE_IF_VLAN_IF_INTERFACES_INTERFACE_ID, p_bridge_node->vlan_id);

    nas_pack_vlan_port_list(obj_pub, &p_bridge_node->tagged_list, DELL_IF_IF_INTERFACES_INTERFACE_TAGGED_PORTS);
    nas_pack_vlan_port_list(obj_pub, &p_bridge_node->untagged_list, DELL_IF_IF_INTERFACES_INTERFACE_UNTAGGED_PORTS);

    cps_api_object_set_type_operation(cps_api_object_key(obj_pub),op);

    if (cps_api_event_thread_publish(obj_pub)!=STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE,3,"NAS-VLAN","Failed to send VLAN publish event.  Service issue");
        return cps_api_ret_code_ERR;
    }
    return cps_api_ret_code_OK;
}
