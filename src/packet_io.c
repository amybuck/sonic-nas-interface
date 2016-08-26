/*
 * Copyright (c) 2016 Dell Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License. You may obtain
 * a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * THIS CODE IS PROVIDED ON AN  *AS IS* BASIS, WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
 * LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
 * FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
 *
 * See the Apache Version 2.0 License for specific language governing
 * permissions and limitations under the License.
 */

/*!
 * \file   packet_io.c
 * \brief  Core functionality for the Packet Tx/Rx
 */
#define _GNU_SOURCE

#include "event_log.h"
#include "event_log_types.h"
#include "std_error_codes.h"

#include "nas_ndi_port.h"
#include "hal_shell.h"
#include "hal_interface_common.h"
#include "hal_interface.h"
#include "nas_int_port.h"
#include "nas_packet_meta.h"
#include "std_socket_tools.h"

#include "cps_class_map.h"
#include "cps_api_operation.h"
#include "cps_api_object_category.h"
#include "dell-base-sflow.h"

#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_tun.h>
#include <string.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#define MAX_PKT_LEN        12000
#define PKT_DBG_ERR        (1)
#define PKT_DBG_DUMP       (1 << 1)

#define PKT_DEBUG(arg...)\
    do {\
        if (!pkt_debug) break;\
        printf("[PKT_IO]: %s:%d: ",__FUNCTION__, __LINE__);\
        printf(arg);\
        printf("\r\n");\
    } while (0)

/*
 * Global variables
 */
static int pkt_debug = 0;

static uint64_t packets_txed;
static uint64_t packets_rxed;
static uint8_t  pkt_buf[MAX_PKT_LEN];

void pkt_debug_counters(std_parsed_string_t handle) {
    printf("RX: %llu, TX: %llu \n",(unsigned long long)packets_rxed,(unsigned long long)packets_txed);
}
/*
 * Pthread variables
 */
static pthread_t packet_io_thr;

static void hal_packet_io_dump(uint8_t *buf, int len)
{
    int var_j, var_n;

    if (pkt_debug == PKT_DBG_DUMP) {
        var_n = 0;
        if (buf != NULL) {
            printf("[PKT_IO]: %s: Dumping Raw Pkt\r\n", __FUNCTION__);
            for (var_j = 0; var_j < len; var_j++) {
                printf("0x%02x ", buf[var_j]);
                if ((var_n != 0) && ((var_n + 1) % 16 == 0))
                    printf("\r\n");
                var_n++;
            }
        }
        printf("\r\n");
    }
}

static int sflow_sock_fd = -1;
static std_socket_address_t sflow_sock_dest;

static void _sflow_sock_init ()
{
    t_std_error rc = std_socket_create (e_std_sock_INET4, e_std_sock_type_DGRAM,
                                        0, NULL, &sflow_sock_fd);
    if (rc != STD_ERR_OK) {
        EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "PKT-IO", "SFlow socket Error %ld", rc);
    }
    /* Set the SFlow pkt dest addr from default values. Will be made configurable in future */
#define SFLOW_PKT_DEF_IP    "127.0.0.1"
#define SFLOW_PKT_DEF_PORT   20001
    rc = std_sock_addr_from_ip_str (e_std_sock_INET4, SFLOW_PKT_DEF_IP,
                                    SFLOW_PKT_DEF_PORT, &sflow_sock_dest);
    if (rc != STD_ERR_OK) {
        EV_LOG_ERR(ev_log_t_INTERFACE, ev_log_s_CRITICAL, "PKT-IO", "SFlow socket address creation error %ld", rc);
    }
}

static t_std_error _sflow_pkt_hdl (uint8_t *pkt, uint32_t pkt_len,
                                   const ndi_packet_attr_t *p_attr)
{
    hal_ifindex_t rx_ifindex;
    if (!nas_int_port_ifindex (p_attr->npu_id, p_attr->rx_port, &rx_ifindex)) {
        EV_LOG (ERR, INTERFACE, ev_log_s_CRITICAL, "PKT-IO",
                 "Interface invalid - no matching port %d:%d",
                p_attr->npu_id, p_attr->rx_port);
        return STD_ERR (INTERFACE, NOMEM, 0);
    }

    PKT_DEBUG("[RX] SFLOW Pkt received - length %d npu-port %d:%d ifindex %d\r\n",
              pkt_len, p_attr->npu_id, p_attr->rx_port, rx_ifindex);

#define META_BUF_SIZE  1024
    uint8_t meta_buf [META_BUF_SIZE];

    nas_pkt_meta_attr_it_t it;
    nas_pkt_meta_buf_init (meta_buf, sizeof(meta_buf), &it);
    nas_pkt_meta_add_u32 (&it, NAS_PKT_META_RX_PORT, rx_ifindex);

    // The length field in the iterator gives the remaining length left
    // after filling all meta data attributes
    size_t meta_len = sizeof(meta_buf) - it.len;
    struct iovec sock_data[] = { {(char*)meta_buf, meta_len},
                                {pkt, pkt_len} };

    /* TODO - Avoid direct access to the global sflow sock dest addr
     *  since we will add support to configure the global.
     *  Will need to copy inet4addr into a local variable in future */
    std_socket_msg_t sock_msg = { &sflow_sock_dest.address.inet4addr,
            sizeof (sflow_sock_dest.address.inet4addr),
            sock_data, sizeof (sock_data)/sizeof (sock_data[0]),
            NULL, 0, 0};

    t_std_error rc;
    int n = std_socket_op (std_socket_transit_o_WRITE, sflow_sock_fd, &sock_msg,
            std_socket_transit_f_NONE, 0, &rc);
    if (n < 0) {
        PKT_DEBUG("[RX] ForwardMsg to UDP socket %d FAILED - Error %d code (%d)\r\n",
                  sflow_sock_fd, rc, STD_ERR_EXT_PRIV(rc));
    }

    return rc;
}

static bool _extract_std_ipv4_sock_addr (const std_socket_address_t* in_saddr,
                                         dn_ipv4_addr_t* out_ip, int *out_port)
{
    if (in_saddr->type != e_std_sock_INET4) return false;
    *out_ip = in_saddr->address.inet4addr.sin_addr;
    *out_port = ntohs (in_saddr->address.inet4addr.sin_port);
    return true;
}

static void _mk_std_ipv4_sock_addr (dn_ipv4_addr_t* ip, int port,
                                    std_socket_address_t* out_saddr)
{
    out_saddr->address.inet4addr.sin_family = AF_INET;
    out_saddr->address.inet4addr.sin_addr = *ip;
    out_saddr->addr_type = e_std_socket_a_t_INET; // Common for all INET families
    out_saddr->type = e_std_sock_INET4;
    out_saddr->address.inet4addr.sin_port = htons (port);
}

static cps_api_return_code_t _cps_api_read (void                 *context,
                                            cps_api_get_params_t *param,
                                            size_t                index)
{
    cps_api_object_t filter_obj = cps_api_object_list_get (param->filters, index);

    if (cps_api_key_get_cat (cps_api_object_key (filter_obj)) != cps_api_obj_CAT_BASE_SFLOW) {
        EV_LOG (ERR, INTERFACE, ev_log_s_MAJOR, "PKT-IO", "Invalid Category");
        return cps_api_ret_code_ERR;
    }

    if (cps_api_key_get_subcat (cps_api_object_key (filter_obj)) != BASE_SFLOW_SOCKET_ADDRESS_OBJ) {
        EV_LOG (ERR, INTERFACE, ev_log_s_MAJOR, "PKT-IO", "Invalid Sub-Category");
        return cps_api_ret_code_ERR;
    }

    cps_api_object_t obj = cps_api_object_list_create_obj_and_append (param->list);
    if (!obj) {
        EV_LOG (ERR, INTERFACE, ev_log_s_MAJOR, "PKT-IO",
                "Obj Append failed. Index: %ld", index);
        return cps_api_ret_code_ERR;
    }

    if (!cps_api_key_from_attr_with_qual (cps_api_object_key (obj),
                                          BASE_SFLOW_SOCKET_ADDRESS_OBJ,
                                          cps_api_qualifier_TARGET)) {
        EV_LOG (ERR, INTERFACE, ev_log_s_MAJOR, "PKT-IO",
                "Failed to create Key from Table Object");
        return cps_api_ret_code_ERR;
    }


    dn_ipv4_addr_t ip;
    int port;
    _extract_std_ipv4_sock_addr (&sflow_sock_dest, &ip, &port);

    if (!cps_api_object_attr_add (obj, BASE_SFLOW_SOCKET_ADDRESS_IP, &ip, sizeof (ip))) {
        return cps_api_ret_code_ERR;
    }

    if (!cps_api_object_attr_add_u16 (obj, BASE_SFLOW_SOCKET_ADDRESS_UDP_PORT, port)) {
        return cps_api_ret_code_ERR;
    }

    return cps_api_ret_code_OK;
}

static cps_api_return_code_t _cps_api_write_int (void                         *context,
                                                 cps_api_transaction_params_t *param,
                                                 size_t                        index,
                                                 cps_api_object_t              prev)
{
    cps_api_object_t          obj;
    cps_api_operation_types_t op;

    obj = cps_api_object_list_get (param->change_list, index);
    if (obj == NULL) {
        EV_LOG (ERR, INTERFACE, ev_log_s_MAJOR, "PKT-IO", "Missing Change Object");
        return cps_api_ret_code_ERR;
    }

    op = cps_api_object_type_operation (cps_api_object_key (obj));
    if (op != cps_api_oper_SET) {
        EV_LOG (ERR, INTERFACE, ev_log_s_MAJOR, "PKT-IO", "Invalid operation");
        return cps_api_ret_code_ERR;
    }

    dn_ipv4_addr_t ip;
    int port;
    _extract_std_ipv4_sock_addr (&sflow_sock_dest, &ip, &port);

    bool dirty=false;
    cps_api_object_it_t  it;

    for (cps_api_object_it_begin (obj, &it);
         cps_api_object_it_valid (&it); cps_api_object_it_next (&it)) {

        cps_api_attr_id_t attr_id = cps_api_object_attr_id (it.attr);

        switch (attr_id) {
            case BASE_SFLOW_SOCKET_ADDRESS_IP:
                if (cps_api_object_attr_len (it.attr) < sizeof (ip)) {
                    EV_LOG (ERR, INTERFACE, ev_log_s_MINOR, "PKT-IO",
                            "Invalid attribute %ld data length", attr_id);
                    return cps_api_ret_code_ERR;
                }
                if (prev != NULL) {
                    cps_api_object_attr_add (prev, BASE_SFLOW_SOCKET_ADDRESS_IP, &ip, sizeof (ip));
                }
                ip = *(dn_ipv4_addr_t*)cps_api_object_attr_data_bin(it.attr);
                dirty = true;
                break;
            case BASE_SFLOW_SOCKET_ADDRESS_UDP_PORT:
                if (prev != NULL) {
                    cps_api_object_attr_add_u16 (prev, BASE_SFLOW_SOCKET_ADDRESS_UDP_PORT, port);
                }
                port = cps_api_object_attr_data_u16 (it.attr);
                dirty = true;
                break;
            default:
                EV_LOG (ERR, INTERFACE, ev_log_s_MINOR, "PKT-IO",
                        "Unknown attribute ignored %d", attr_id);
                break;
        }
    }
    if (dirty) _mk_std_ipv4_sock_addr (&ip, port, &sflow_sock_dest);
    return cps_api_ret_code_OK;
}

static cps_api_return_code_t _cps_api_write (void                         *context,
                                             cps_api_transaction_params_t *param,
                                             size_t                        index)
{
    cps_api_object_t prev = cps_api_object_list_create_obj_and_append (param->prev);
    if (prev == NULL) {
        return cps_api_ret_code_ERR;
    }

    return _cps_api_write_int (context, param, index, prev);
}

static cps_api_return_code_t _cps_api_rollback (void                         *context,
                                                cps_api_transaction_params_t *param,
                                                size_t                        index)
{
    return _cps_api_write_int (context, param, index, NULL);
}

static t_std_error _cps_init ()
{
    cps_api_operation_handle_t       handle;
    cps_api_return_code_t            rc;
    cps_api_registration_functions_t f;

    rc = cps_api_operation_subsystem_init (&handle,1);

    if (rc != cps_api_ret_code_OK) {
        EV_LOG (ERR, INTERFACE, ev_log_s_CRITICAL, "PKT-IO",
                 "CPS Subsystem Init failed");
        return STD_ERR(INTERFACE, FAIL, rc);
    }

    memset (&f, 0, sizeof(f));

    f.handle             = handle;
    f._read_function     = _cps_api_read;
    f._write_function    = _cps_api_write;
    f._rollback_function = _cps_api_rollback;

    /* Register SFLOW model socket address object */
    cps_api_key_init (&f.key,
                      cps_api_qualifier_TARGET,
                      cps_api_obj_CAT_BASE_SFLOW,
                      BASE_SFLOW_SOCKET_ADDRESS_OBJ, /* register all sub-categories */
                      0);

    rc = cps_api_register (&f);

    if (rc != cps_api_ret_code_OK) {
        EV_LOG (ERR, INTERFACE, ev_log_s_CRITICAL, "PKT-IO",
                "CPS object Register failed");
        return STD_ERR(INTERFACE, FAIL, rc);
    }

    return STD_ERR_OK;
}

/*!
 *  \brief     Function to receive packet from Npu and writes to kernel
 *  \param[in] pkt    The pointer to packet buffer
 *  \param[in] len    The length of packet
 *  \param[in] attr   The packet attribute list
 *  \return    std_error
 *  \sa dn_hal_packet_tx
 */

static t_std_error dn_hal_packet_rx(uint8_t *pkt, uint32_t len, ndi_packet_attr_t *p_attr)
{
    ++packets_rxed;
    PKT_DEBUG("[RX] on front npu %d port %d len %d",p_attr->npu_id,p_attr->rx_port,len);
    if (pkt_debug == PKT_DBG_DUMP) hal_packet_io_dump(pkt, len);

    if (p_attr->trap_id == NDI_PACKET_TRAP_ID_SAMPLEPACKET)
        return _sflow_pkt_hdl (pkt, len, p_attr);

    t_std_error err = hal_virtual_interace_send(p_attr->npu_id,p_attr->rx_port,0,pkt,len);
    PKT_DEBUG("[RX] Data written to fd %d", err);
    return (STD_ERR_OK);
}

static void dn_hal_packet_tx(npu_id_t npu, npu_port_t port, void  *pkt, uint32_t len)
{
    ndi_packet_attr_t attr;

    ++packets_txed;
    PKT_DEBUG("[TX] for npu %d port %d len %d\r\n",npu,port,len);

    if (pkt_debug == PKT_DBG_DUMP) hal_packet_io_dump(pkt, len);

    attr.npu_id  = npu;
    attr.tx_port = port;

    if (ndi_packet_tx(pkt, len, &attr) != STD_ERR_OK) {
        PKT_DEBUG("[TX] Pkt txmission FAILED \r\n");
    } else {
        PKT_DEBUG("[TX] Pkt txmission OK \r\n");
    }
}

static void change_debug_flag_state(std_parsed_string_t handle) {
    if(std_parse_string_num_tokens(handle)==0) return;
    size_t ix = 0;
    pkt_debug = strstr(std_parse_string_next(handle,&ix),"true")!=NULL ? 1 : 0;
}

t_std_error hal_packet_io_main(void) {

    while (1) {
        if (hal_virtual_interface_wait(dn_hal_packet_tx,pkt_buf,MAX_PKT_LEN)!=STD_ERR_OK) {
            //XXX todo log error
        }
    }

    return STD_ERR_OK;
}

t_std_error hal_packet_io_init(void)
{
    int error;

    EV_LOG_TRACE(ev_log_t_INTERFACE, 3, "PKT-IO", "Initializing packet I/O Thread");

    error = pthread_create(&packet_io_thr, NULL, (void *)hal_packet_io_main, NULL);
    if (error) {
        EV_LOG_ERR(ev_log_t_INTERFACE, 3, "PKT-IO", "Error %s", strerror(error));
    }

    pthread_setname_np(packet_io_thr, "hal_packet_io");

    /* Create socket to send SFLOW sample packet */
    _sflow_sock_init ();
    _cps_init ();

    ndi_packet_rx_register(dn_hal_packet_rx);

    //hal_shell_cmd_add("pkt-io-debug",change_debug_flag_state,"[true|false] Changes Debug flag state\nWarning: Enabling this will generate lots of information and may impact the performance");
    //hal_shell_cmd_add("pkt-io-counters",pkt_debug_counters,"Displays Packet count");

    return STD_ERR_OK;
}
