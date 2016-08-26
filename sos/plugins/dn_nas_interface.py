#
# Copyright (c) 2015 Dell Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may
# not use this file except in compliance with the License. You may obtain
# a copy of the License at http://www.apache.org/licenses/LICENSE-2.0
#
# THIS CODE IS PROVIDED ON AN *AS IS* BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT
# LIMITATION ANY IMPLIED WARRANTIES OR CONDITIONS OF TITLE, FITNESS
# FOR A PARTICULAR PURPOSE, MERCHANTABLITY OR NON-INFRINGEMENT.
#
# See the Apache Version 2.0 License for specific language governing
# permissions and limitations under the License.
#

from sos.plugins import Plugin, DebianPlugin
import os

class DN_nas_interfacePlugin(Plugin, DebianPlugin):
    """ Collects nas debugging information
    """

    plugin_name = os.path.splitext(os.path.basename(__file__))[0]
    profiles = ('networking', 'dn')

    def setup(self):
        self.add_cmd_output("/opt/dell/os10/bin/os10-show-transceivers all")
        self.add_cmd_output("/opt/dell/os10/bin/cps_get_oid.py base-if-phy/hardware-port")
        self.add_cmd_output("/opt/dell/os10/bin/cps_get_oid.py base-if-phy/front-panel-port")
        self.add_cmd_output("/opt/dell/os10/bin/cps_get_oid.py base-if-phy/physical")
        self.add_cmd_output("/opt/dell/os10/bin/cps_get_oid.py dell-base-if-cmn/if/interfaces/interface if/interfaces/interface/type=ianaift:ethernetCsmacd")
        self.add_cmd_output("/opt/dell/os10/bin/os10-show-stats if_stat")
        self.add_cmd_output("/opt/dell/os10/bin/cps_get_oid.py dell-base-if-cmn/if/interfaces/interface if/interfaces/interface/type=ianaift:l2vlan")
        self.add_cmd_output("/opt/dell/os10/bin/cps_get_oid.py dell-base-if-cmn/if/interfaces/interface if/interfaces/interface/type=ianaift:ieee8023adLag")
        self.add_cmd_output("/opt/dell/os10/bin/cps_get_oid.py observed dell-base-if-cmn/if/interfaces-state/interface if/interfaces-state/interface/type=ianaift:ethernetCsmacd")
