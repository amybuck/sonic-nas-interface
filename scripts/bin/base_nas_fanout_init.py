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
import cps_object
from xml.dom import minidom
import syslog
from time import sleep
import os
import subprocess
import cps
import nas_os_if_utils as nas_if


fanout_config_file = "/etc/sonic/dn_nas_fanout_init_config.xml"

def parse_intf_config(intf_list):
    """parse the fanout config from xml and
       create fanned out interfaces based on that
       @intf_list - xml tag list of fanout config
    """
    try:
        for i in intf_list:
            interface = i.attributes["name"].value
            is_fanout = i.attributes["fanout"].value
            if is_fanout == "true":
                subprocess.call(["/usr/bin/python",
                                 "/usr/bin/sonic-config-fanout",interface,"true"])
    except Exception as ex:
        syslog.syslog(str(ex))


def parse_config_file():
    """parse the xml file which has the config for
       fanout interfaces
    """
    if not os.path.isfile(fanout_config_file):
        print "%s does not exist" % (fanout_config_file)
    try:
        config_xml_handle = minidom.parse(fanout_config_file)
    except Exception:
        print "%s Is not a valid xml file" % (fanout_config_file)

    try:
        intf_list = config_xml_handle.getElementsByTagName("interface")
        parse_intf_config(intf_list)
    except Exception as ex:
        syslog.syslog(str(ex))


if __name__ == '__main__':

    # Wait till interface service is ready
    while nas_if.nas_os_if_list() == None:
        sleep(1)

    parse_config_file()
