
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
 * nas_int_port.cpp
 *
 *  Created on: Jun 9, 2015
 */


#include "hal_if_mapping.h"
#include "hal_interface_common.h"
#include "dell-base-if-phy.h"

#include "swp_util_tap.h"

#include "std_error_codes.h"
#include "event_log.h"
#include "std_assert.h"
#include "std_rw_lock.h"
#include "std_time_tools.h"


#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define MAX_QUEUE          1

//Lock for a interface structures
static std_rw_lock_t ports_lock = PTHREAD_RWLOCK_INITIALIZER;


class CNasPortDetails {
    bool _used = false;
    npu_id_t _npu = 0;
    port_t _port = 0;
    uint32_t _hwport = 0;
    swp_util_tap_descr _dscr=nullptr;
    IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_t _link = IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_DOWN;
public:
    void init(npu_id_t npu, port_t port) {
        _npu = npu;
        _port=port;
    }

    IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_t link_state() {
        return _link;
    }

    void set_link_state(IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_t state) ;

    bool del();
    bool create(const char *name);
    hal_ifindex_t ifindex() ;
    bool valid() { return _used; }

    inline npu_id_t npu() { return _npu; }
    inline port_t port() { return _port; }
    inline swp_util_tap_descr tap() { return _dscr; }

    ~CNasPortDetails();

};

using NasPortList = std::vector<CNasPortDetails> ;
using NasListOfPortList = std::vector<NasPortList> ;

static NasListOfPortList _ports;

struct tap_and_npu_details {
    CNasPortDetails *details;
    swp_util_tap_descr tap;
};

using NasTapList = std::vector<tap_and_npu_details> ;
static NasTapList _taps;

//set of FDs that we listen to for changes on tap along with max FD
static fd_set tap_fds;
static int max_tap_fd=-1;

/**
 * when the tap info changes, call this function to reset the tap_fds and max_tap_fd
 */
static void update_tap_fd_info() {
    FD_ZERO(&tap_fds);
    size_t ix = 0;
    size_t mx = _taps.size();
    for ( ; ix < mx ; ++ix) {
        int mx_fd = swp_util_tap_fd_set_add(&(_taps[ix].tap), 1, &tap_fds);
        max_tap_fd = std::max(mx_fd, max_tap_fd);
    }
}

static bool tap_link_down(swp_util_tap_descr tap) {
    swp_util_close_fds(tap);
    update_tap_fd_info();
    return true;
}

static bool tap_link_up(swp_util_tap_descr tap) {
    //just incase... clean up
    swp_util_close_fds(tap);

    size_t retry = 0;
    const size_t MAX_RETRY = 12;
    bool success = false;
    for ( ; retry < MAX_RETRY ; ++retry ) {
        if (swp_util_alloc_tap(tap,SWP_UTIL_TYPE_TAP)!=STD_ERR_OK) {
            EV_LOG(ERR,INTERFACE,0,"INT-LINK",
                    "Can not bring the link up %s had issues opening device (%d)",
                    swp_util_tap_descr_get_name(tap),retry);
            std_usleep(MILLI_TO_MICRO((1<<retry)));
            continue;
        }
        success = true;
        break;
    }
    if (!success) {
        return false;
    }
    update_tap_fd_info();
    EV_LOG(TRACE,INTERFACE,0,"INT-LINK", "Link up %s ",swp_util_tap_descr_get_name(tap));
    return true;
}

static void tap_delete(swp_util_tap_descr tap) {

    tap_link_down(tap);

    size_t ix = 0;
    size_t mx = _taps.size();
    for ( ; ix < mx ; ++ix ) {
        if (_taps[ix].tap == tap) {
            _taps.erase(_taps.begin()+ix);
        }
    }

    const char * name = swp_util_tap_descr_get_name(tap);

    if (swp_util_tap_operation(name,SWP_UTIL_TYPE_TAP,false)!=STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE,0,"INTF-DEL", "Failed to delete linux interface %s",name);
    }

    swp_util_free_descrs(tap);
}

static swp_util_tap_descr tap_create(CNasPortDetails *npu, const char * name, uint_t queues) {
    swp_util_tap_descr tap = swp_util_alloc_descr();
    if (tap==nullptr) return tap;

    swp_util_tap_descr_init_wname(tap,name,NULL,NULL,NULL,NULL,queues);

    if (swp_util_tap_operation(name,SWP_UTIL_TYPE_TAP,true)!=STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE,0,"INTF-DEL", "Failed to delete linux interface %s",name);
    }
    tap_and_npu_details d ;
    d.details = npu;
    d.tap = tap;
    _taps.push_back(d);
    return tap;
}

CNasPortDetails::~CNasPortDetails() {

}

bool CNasPortDetails::del() {
    _used = false;
    if (_dscr==nullptr) return true;
    set_link_state(IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_DOWN);

    tap_delete(_dscr);
    _dscr = nullptr;
    return true;
}

bool CNasPortDetails::create(const char *name) {
    if (_used) return true;
    _used = true;
    _dscr = tap_create(this,name,MAX_QUEUE);
    return true;
}

void CNasPortDetails::set_link_state(IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_t state)  {
    if (_link==state) return;
    if (state == IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_UP) {
        if (tap_link_up(_dscr)) _link=state;
    }
    if (state == IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_DOWN) {
        tap_link_down(_dscr);
        _link = state;
    }
}

hal_ifindex_t CNasPortDetails::ifindex(){
    if (_dscr==nullptr) return -1;
    return swp_init_tap_ifindex(_dscr);
}

static const ssize_t INVALID_INDEX=~0l;

static size_t find_first_tap_matching(size_t ix,fd_set &rds) {
    size_t mx = _taps.size();
    for ( ; ix < mx ; ++ix ) {
        int fd = swp_util_tap_fd_locate_from_set(_taps[ix].tap, &rds);
        if (fd!=SWP_UTIL_INV_FD) return ix;
    }
    return INVALID_INDEX;
}

static void process_packets(size_t ix, fd_set &rds, void *data, unsigned int len,
        hal_virt_pkt_transmit fun) {
    if (ix > _taps.size()) return;
    swp_util_tap_descr tap = _taps[ix].tap;
    npu_id_t npu = _taps[ix].details->npu();
    port_t port = _taps[ix].details->port();

    while (true) {
        int fd = swp_util_tap_fd_locate_from_set(tap, &rds);
        if (fd < 0)
            break;
        FD_CLR(fd, &rds);

        size_t pkt_len = read(fd, data, len);
        if (pkt_len>0) {
            fun(npu,port,data,pkt_len);
        }
    }
}

extern "C" t_std_error hal_virtual_interface_wait(hal_virt_pkt_transmit fun,
        void *data, unsigned int len) {

    fd_set  rds;

    int     max_fd = 0;

    {
        std_rw_lock_read_guard l(&ports_lock);
        max_fd = max_tap_fd;
        memcpy(&rds,&tap_fds,sizeof(rds));
    }

    int rc = 0;

    do {
        struct timeval tv = { 1,0};

        if ((rc = select(max_fd + 1, &rds, NULL, NULL, &tv))) {
            if (rc < 0 || errno == EINTR)
                return STD_ERR(INTERFACE,FAIL,0);
            if (rc == 0)
                return STD_ERR_OK;

            std_rw_lock_read_guard l(&ports_lock);
            ssize_t ix = 0;
            while (true) {
                ix = find_first_tap_matching(ix,rds);
                if (ix==INVALID_INDEX) break;
                process_packets(ix,rds,data,len,fun);
                ++ix;
            }
        }
    }while (0);
    return STD_ERR_OK;
}

extern "C" t_std_error hal_virtual_interace_send(npu_id_t npu, npu_port_t port, int queue,
        const void * data, unsigned int len) {
    int fd = -1;
    {
        std_rw_lock_read_guard l(&ports_lock);
        if (_ports[npu][port].link_state()!=IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_UP) {
            return STD_ERR_OK;
        }
        fd = swp_util_tap_descr_get_queue(_ports[npu][port].tap(), queue);
    }

    if (fd!=-1) {
        int l = write(fd,data,len);
        if (((int)len)!=l) return STD_ERR(INTERFACE,FAIL,errno);
    }
    return STD_ERR_OK;
}

extern "C" bool nas_int_port_used(npu_id_t npu, port_t port) {
    std_rw_lock_read_guard l(&ports_lock);
    return _ports[npu][port].valid();
}

extern "C" bool nas_int_port_ifindex (npu_id_t npu, port_t port, hal_ifindex_t *ifindex) {
    std_rw_lock_read_guard l(&ports_lock);
    if (!_ports[npu][port].valid()) {
        return false;
    }
    *ifindex = _ports[npu][port].ifindex ();
    return true;
}

extern "C" void nas_int_port_link_change(npu_id_t npu, port_t port,
                    IF_INTERFACES_STATE_INTERFACE_OPER_STATUS_t state) {
    std_rw_lock_write_guard l(&ports_lock);

    if (!_ports[npu][port].valid()) {
        EV_LOG(ERR,INTERFACE,0,"INT-STATE", "Interface state invalid - no matching port %d:%d",(int)npu,(int)port);
        return;
    }

    _ports[npu][port].set_link_state(state);
    EV_LOG(INFO,INTERFACE,0,"INT-STATE", "Interface state change %d:%d to %d",(int)npu,(int)port,(int)state);
}

extern "C" t_std_error nas_int_port_create(npu_id_t npu, port_t port, const char *name) {

    std_rw_lock_write_guard l(&ports_lock);


    //if created already... return error
    if (_ports[npu][port].valid()) return STD_ERR(INTERFACE,PARAM,0);

    _ports[npu][port].init(npu,port);

    if (!_ports[npu][port].create(name)) {
        EV_LOG(ERR,INTERFACE,0,"INT-CREATE", "Not created %d:%d:%s - error in create",
                (int)npu,(int)port,name);
        return STD_ERR(INTERFACE,FAIL,0);
    }

    interface_ctrl_t details;
    memset(&details,0,sizeof(details));
    details.if_index = _ports[npu][port].ifindex();
    strncpy(details.if_name,name,sizeof(details.if_name)-1);
    details.npu_id = npu;
    details.port_id = port;
    details.tap_id = (npu << 16) + port;
    details.int_type = nas_int_type_PORT;

    if (dn_hal_if_register(HAL_INTF_OP_REG,&details)!=STD_ERR_OK) {
        EV_LOG(ERR,INTERFACE,0,"INT-CREATE", "Not created %d:%d:%s - mapping error",
                        (int)npu,(int)port,name);
        _ports[npu][port].del();
        return STD_ERR(INTERFACE,FAIL,0);
    }

    EV_LOG(INFO,INTERFACE,0,"INT-CREATE", "Interface created %d:%d:%s - %d ",
            (int)npu,(int)port,name, details.if_index);

    return STD_ERR_OK;
}

extern "C" t_std_error nas_int_port_delete(npu_id_t npu, port_t port) {
    std_rw_lock_write_guard l(&ports_lock);

    interface_ctrl_t details;

    memset(&details,0,sizeof(details));
    details.if_index = _ports[npu][port].ifindex();
    details.q_type = HAL_INTF_INFO_FROM_IF;

    if (dn_hal_get_interface_info(&details)==STD_ERR_OK) {
        dn_hal_if_register(HAL_INTF_OP_DEREG,&details);
    }

    if (!_ports[npu][port].del()) {
        return STD_ERR(INTERFACE,FAIL,0);
    }
    return STD_ERR_OK;
}

extern "C" t_std_error nas_int_port_init(void) {

    std_rw_lock_write_guard l(&ports_lock);
    size_t npus = 1; //!@TODO get the maximum ports

    _ports.resize(npus);

    for ( size_t npu_ix = 0; npu_ix < npus ; ++npu_ix ) {
        size_t port_mx = ndi_max_npu_port_get(npu_ix)*4;
        _ports[npu_ix].resize(port_mx);
    }
    return STD_ERR_OK;
}

