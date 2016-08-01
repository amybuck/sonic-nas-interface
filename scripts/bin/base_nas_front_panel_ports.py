#!/usr/bin/python
#
# Copyright (c) 2015 Dell Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may
# not use this file except in compliance with the License. You may obtain
# a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
#
# THIS CODE IS PROVIDED ON AN #AS IS* BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
#  LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
# FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
#
# See the Apache Version 2.0 License for specific language governing
# permissions and limitations under the License.
#

import cps
import cps_object
import cps_utils
import nas_front_panel_map as fp
import nas_os_if_utils as nas_if
import bytearray_utils as ba

import sys
import time
import xml.etree.ElementTree as ET
from nas_front_panel_map import NPU
import copy

_breakout_key = cps.key_from_name('target', 'base-if-phy/breakout')
_fp_key = cps.key_from_name('target', 'base-if-phy/front-panel-port')
_npu_lane_key = cps.key_from_name('target', 'base-if-phy/hardware-port')
_set_intf_key = cps.key_from_name('target', 'dell-base-if-cmn/set-interface')

npu_attr_name = 'base-if-phy/if/interfaces/interface/npu-id'
port_attr_name = 'base-if-phy/if/interfaces/interface/port-id'
fp_port_attr_name = 'base-if-phy/hardware-port/front-panel-port'
subport_attr_name = 'base-if-phy/hardware-port/subport-id'
ifindex_attr_name = 'dell-base-if-cmn/if/interfaces/interface/if-index'

def _breakout_i_attr(t):
    return 'base-if-phy/breakout/input/' + t


def _fp_attr(t):
    return 'base-if-phy/front-panel-port/' + t


def _lane_attr(t):
    return 'base-if-phy/hardware-port/' + t


def _gen_fp_port_list(obj, resp):

    for npu in fp.get_npu_list():

        for p in npu.ports:

            port = npu.ports[p]

            if not obj.key_compare({_fp_attr('front-panel-port'): port}):
                continue

            media_id = port.media_id

            if not obj.key_compare({_fp_attr('media-id'): media_id}):
                continue

            elem = cps_object.CPSObject(module='base-if-phy/front-panel-port',
                                        data={
                                        'npu-id': npu.id,
                                        'front-panel-port': port.id,
                                        'control-port': port.control_port(),
                                        'port': port.hwports,
                                        'default-name': port.name,
                                        'media-id': media_id,
                                        'mac-offset':port.mac_offset,
                                        })

            resp.append(elem.get())


def get_phy_port_cache_keys(npu, port):
    return "port-%d-%d" % (npu, port)


def get_phy_port_cache_hw_keys(npu, port):
    return "hw-%d-%d" % (npu, port)


def get_phy_port_cache():
    l = []
    m = {}
    if not cps.get([cps_object.CPSObject(module='base-if-phy/physical').get()], l):
        return

    for i in l:
        ph = cps_object.CPSObject(obj=i)
        m[get_phy_port_cache_hw_keys(ph.get_attr_data('npu-id'),
                                     ph.get_attr_data('hardware-port-id'))] = ph
        m[get_phy_port_cache_keys(ph.get_attr_data('npu-id'),
                                  ph.get_attr_data('port-id'))] = ph
    return m


def _gen_npu_lanes(obj, resp):
    m = get_phy_port_cache()

    for npu in fp.get_npu_list():
        key_dict = {_lane_attr('npu-id'): npu.id
                     }

        if not obj.key_compare(key_dict):
            continue

        for p in npu.ports:
            port = npu.ports[p]

            for h in port.hwports:
                if not obj.key_compare({_lane_attr('hw-port'): h}):
                    continue

                key = get_phy_port_cache_hw_keys(npu.id, h)

                pm = 4  # port mode 4 is disabled
                if key in m:
                    pm = m[key].get_attr_data('fanout-mode')

                elem = cps_object.CPSObject(module='base-if-phy/hardware-port',
                                            data= {
                                            'npu-id': npu.id,
                                            'hw-port': h,
                                            'front-panel-port': port.id,
                                            'hw-control-port':
                                                port.control_port(),
                                            'subport-id': port.lane(h),
                                            'fanout-mode': pm
                                            })
                print elem.get()
                resp.append(elem.get())


def get_cb(methods, params):
    obj = cps_object.CPSObject(obj=params['filter'])
    resp = params['list']

    if obj.get_key() == _fp_key:
        _gen_fp_port_list(obj, resp)
    elif obj.get_key() == _npu_lane_key:
        _gen_npu_lanes(obj, resp)
    else:
        return False

    return True


def hw_port_to_nas_port(ports, npu, hwport):
    ph = get_phy_port_cache_hw_keys(npu, hwport)
    if ph in ports:
            return ports[ph].get_attr_data('port-id')
    return -1


def set_cb(methods, params):
    print params
    obj = cps_object.CPSObject(obj=params['change'])

    if params['operation'] != 'rpc':
        return False

    fr_port = obj.get_attr_data(_breakout_i_attr('front-panel-port'))
    mode = obj.get_attr_data(_breakout_i_attr('breakout-mode'))

    port_obj = fp.find_front_panel_port(fr_port)
    if port_obj is None:
        return False

    m = get_phy_port_cache()

    npu = port_obj.npu

    control_port = hw_port_to_nas_port(
        m,
        npu,
        port_obj.control_port())
    if control_port == -1:
        return False

    port_list = []

    if mode == 2:  # breakout - 1->4
        port_list.append(control_port)
    if mode == 4:  # breakin 4->1
        for i in port_obj.hwports:
            port_list.append(hw_port_to_nas_port(m, npu, i))

    for i in port_list:
        if i == -1:
            print "Invalid port list detected.. not able to complete operation "
            print port_list
            return False

    breakout_req = cps_object.CPSObject(module='base-if-phy/set-breakout-mode',
                                        data={
                                        'base-if-phy/set-breakout-mode/input/breakout-mode':
                                        mode,
                                        'base-if-phy/set-breakout-mode/input/npu-id':
                                            npu,
                                        'base-if-phy/set-breakout-mode/input/port-id':
                                        control_port,
                                        'base-if-phy/set-breakout-mode/input/effected-port':
                                        port_list
                                        })

    tr = cps_utils.CPSTransaction([('rpc', breakout_req.get())])
    if tr.commit() == False:
        return False

    return True

if_mac_info_cache = {}
def get_mac_addr_base_range(if_type):
    if len(if_mac_info_cache) == 0:
        cfg = ET.parse('/etc/sonic/mac_address_alloc.xml')
        root = cfg.getroot()
        for i in root.findall('interface'):
            type_name = i.get('type')
            base = int(i.get('base-offset'))
            off_range = int(i.get('offset-range'))
            if_mac_info_cache[type_name] = (base, off_range)
            print '%-15s: base %d range %d' % (type_name, base, off_range)
    if not if_type in if_mac_info_cache:
        print 'Unknown interface type %s' % if_type
        return None
    return if_mac_info_cache[if_type]

def get_offset_mac_addr(base_addr, offset):
    if isinstance(base_addr, str):
        if base_addr.find(':') >= 0:
            base_addr = ''.join(base_addr.split(':'))
        arr = [int(base_addr[i:i+2],16) for i in range(0, len(base_addr), 2)]
    elif isinstance(base_addr, bytearray):
        arr = copy.copy(base_addr)
    else:
        print 'Invalid mac address type'
        return None
    idx = len(arr)
    while idx > 0:
        addr_num = arr[idx - 1] + offset
        arr[idx - 1] = addr_num % 256
        offset = addr_num / 256
        if offset == 0:
            break
        idx -= 1
    return ':'.join('%02x' % x for x in arr)

base_mac_addr = None
def get_base_mac_addr():
    global base_mac_addr
    if base_mac_addr == None:
        base_mac_addr = nas_if.get_base_mac_address()
    return base_mac_addr

def if_get_mac_addr(if_type, npu_id = None, hw_port = None, vlan_id = None, lag_id = None):
    base_mac = get_base_mac_addr()
    base_range = get_mac_addr_base_range(if_type)
    if base_range == None:
        print 'Failed to get mac addr base and range for if type %s' % if_type
        return None
    (base_offset, addr_range) = base_range
    mac_offset = 0
    get_mac_offset = lambda boff, brange, val: boff + val % brange
    if if_type == 'front-panel':
        if hw_port == None or npu_id == None:
            print 'No hardware port id or npu id for front panel port'
            return None
        npu = fp.get_npu(npu_id)
        if npu == None:
            return None
        p = npu.port_from_hwport(hw_port)
        lane = p.lane(hw_port)
        mac_offset = p.mac_offset + lane
    elif if_type == 'vlan':
        if vlan_id == None:
            print 'No VLAN id for VLAN port'
            return None
        mac_offset = get_mac_offset(base_offset, addr_range, vlan_id)
    elif if_type == 'lag':
        if lag_id == None:
            print 'No LAG id for LAG port'
            return None
        mac_offset = get_mac_offset(base_offset, addr_range, lag_id)
    elif if_type == 'management':
        mac_offset = base_offset
    else:
        print 'if type %s not supported' % if_type
        return None

    mac_addr = get_offset_mac_addr(base_mac, mac_offset)
    if mac_addr == None:
        print 'Failed to calculate mac address with offset'
        return None
    return mac_addr

def get_npu_hwport_id_from_fp_obj(cps_obj):
    try:
        npu_id = cps_obj.get_attr_data(npu_attr_name)
    except ValueError:
        print 'Input object does not contain npu id attribute'
        return None
    port_id = None
    try:
        port_id = cps_obj.get_attr_data(port_attr_name)
    except ValueError:
        pass
    if port_id == None:
        try:
            front_panel_port = cps_obj.get_attr_data(fp_port_attr_name)
            subport_id = cps_obj.get_attr_data(subport_attr_name)
        except ValueError:
            print 'front_panel_port or subport not specified'
            return None
        port_obj = fp.find_front_panel_port(front_panel_port)
        if port_obj == None:
            print 'Invalid front panel port id %d' % front_panel_port
            return None
        if subport_id >= len(port_obj.hwports):
            print 'Invalid subport id %d' % subport_id
            return None
        hw_port = port_obj.hwports[subport_id]
        m = get_phy_port_cache()
        port_id = hw_port_to_nas_port(m, npu_id, hw_port)
        if port_id == -1:
            print 'There is no physical mapped to hw_port %d' % hw_port
            return None
        cps_obj.add_attr(port_attr_name, port_id)
    else:
        m = get_phy_port_cache()
        ph_key = get_phy_port_cache_keys(npu_id, port_id)
        if not ph_key in m:
            print 'Physical port object not found'
            return None
        hw_port = m[ph_key].get_attr_data('hardware-port-id')

    return (npu_id, hw_port)

def get_lag_id_from_name(lag_name):
    idx = 0
    while idx < len(lag_name):
        if lag_name[idx].isdigit():
            break
        idx += 1
    if idx >= len(lag_name):
        lag_id = 0
    else:
        lag_id_str = lag_name[idx:]
        if lag_id_str.isdigit():
            lag_id = int(lag_id_str)
        else:
            lag_id = 0
    return lag_id

def set_intf_cb(methods, params):
    op_id_to_name_map = {1: 'create', 2: 'delete', 3: 'set'}
    if params['operation'] != 'rpc':
        print 'Operation %s not supported' % params['operation']
        return False
    op_attr_name = 'dell-base-if-cmn/set-interface/input/operation'
    cps_obj = cps_object.CPSObject(obj = params['change'])
    try:
        op_id = cps_obj.get_attr_data(op_attr_name)
    except ValueError:
        print 'No operation attribute in object'
        return False
    if not op_id in op_id_to_name_map:
        print 'Invalid operation type %d' % op_id
        return False
    op = op_id_to_name_map[op_id]

    if op == 'create':
        if_type_map = {'ianaift:ethernetCsmacd': 'front-panel',
                       'ianaift:l2vlan': 'vlan',
                       'ianaift:ieee8023adLag': 'lag',
                       'base-if:management': 'management'}
        try:
            obj_if_type = cps_obj.get_attr_data('if/interfaces/interface/type')
        except:
            print 'No type attribute in object'
            return False
        if not obj_if_type in if_type_map:
            print 'Unknown if type: %s' % obj_if_type
            return False
        if_type = if_type_map[obj_if_type]

        print 'Create interface, type: %s' % if_type
        hw_port = None
        vlan_id = None
        lag_id = None
        npu_id = None
        if if_type == 'front-panel':
            id_tuple = get_npu_hwport_id_from_fp_obj(cps_obj)
            if id_tuple == None:
                return False
            (npu_id, hw_port) = id_tuple
        elif if_type == 'vlan':
            vlan_id = cps_obj.get_attr_data('base-if-vlan/if/interfaces/interface/id')
        elif if_type == 'lag':
            lag_name = cps_obj.get_attr_data('if/interfaces/interface/name')
            lag_id = get_lag_id_from_name(lag_name)
        elif if_type == 'management':
            pass
        mac_attr_name = 'dell-if/if/interfaces/interface/phys-address'
        mac_addr = None
        try:
            mac_addr = cps_obj.get_attr_data(mac_attr_name)
        except ValueError:
            pass
        if mac_addr == None:
            mac_addr = if_get_mac_addr(if_type, npu_id = npu_id, hw_port = hw_port,
                                       vlan_id = vlan_id,
                                       lag_id = lag_id)
            if mac_addr == None:
                print 'Failed to get mac address'
                return False
            print 'Assigned mac address: %s' % mac_addr
            cps_obj.add_attr(mac_attr_name, mac_addr)
    module_name = nas_if.get_if_key()
    in_obj = copy.deepcopy(cps_obj)
    in_obj.set_key(cps.key_from_name('target', module_name))
    in_obj.root_path = module_name + '/'
    obj = in_obj.get()
    if op_attr_name in obj['data']:
        del obj['data'][op_attr_name]
    upd = (op, obj)
    ret_data = cps_utils.CPSTransaction([upd]).commit()
    if ret_data == False:
        print 'Failed to commit request'
        return False
    if op == 'delete':
        return True
    if len(ret_data) == 0 or not 'change' in ret_data[0]:
        print 'Invalid return object from cps request'
        return False
    ret_obj = cps_object.CPSObject(obj = ret_data[0]['change'])
    try:
        ifindex = ret_obj.get_attr_data(ifindex_attr_name)
    except ValueError:
        print 'Ifindex not found from returned object'
        return False
    try:
        mac_addr = cps_obj.get_attr_data(mac_attr_name)
    except ValueError:
        print 'MAC address not found from returned object'
        return False
    cps_obj = cps_object.CPSObject(obj = params['change'])
    cps_obj.add_attr(ifindex_attr_name, ifindex)
    cps_obj.add_attr(mac_attr_name, mac_addr)
    params['change'] = cps_obj.get()
    return True

if __name__ == '__main__':
    # Wait for base MAC address to be ready. the script will wait until
    # chassis object is registered.
    chassis_key = cps.key_from_name('observed','base-pas/chassis')
    while cps.enabled(chassis_key)  == False:
        #wait for chassis object to be ready
        print 'Create Interface: Base MAC address is not yet ready'
        time.sleep(1)
    fp.init('/etc/sonic/base_port_physical_mapping_table.xml')

    handle = cps.obj_init()

    d = {}
    d['get'] = get_cb
    d['transaction'] = set_cb

    cps.obj_register(handle, _fp_key, d)
    cps.obj_register(handle, _npu_lane_key, d)
    cps.obj_register(handle, _breakout_key, d)

    d = {}
    d['transaction'] = set_intf_cb

    cps.obj_register(handle, _set_intf_key, d)

    while True:
        time.sleep(1)
