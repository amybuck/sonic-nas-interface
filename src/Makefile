
LIBRARIES=interface nas_packet_meta packet_io

UNIT_TEST=nas_vlan_cps_unittest
UNIT_TEST+=nas_int_lag_unittest
UNIT_TEST+=nas_int_phy_media_unittest
UNIT_TEST+=nas_int_loopback_unittest
UNIT_TEST+=nas_stats_ut
UNIT_TEST+=nas_pkt_meta_send_ut nas_pkt_meta_rcv_ut
UNIT_TEST+=nas_int_rpc_unittest

interface_SRCS=swp_util_tap.c nas_int_main.cpp nas_int_common_obj.cpp\
               nas_int_list.c nas_int_ev_handlers.cpp

interface_SRCS += nas_int_bridge.cpp nas_int_vlan.cpp

interface_SRCS+= nas_vlan_cps.cpp

interface_SRCS+= lag/nas_int_lag.c
interface_SRCS+= lag/nas_int_lag_api.cpp
interface_SRCS+= lag/nas_int_lag_cps.cpp

interface_SRCS+=port/hal_int_utils.c
interface_SRCS+=port/nas_int_logical_cps.cpp
interface_SRCS+=port/nas_int_port.cpp
interface_SRCS+=port/nas_int_breakout.cpp
interface_SRCS+=port/nas_int_physical_cps.cpp

interface_SRCS+=stats/nas_stats_if_cps.cpp
interface_SRCS+=stats/nas_stats_vlan_cps.cpp
interface_SRCS+=nas_vlan_lag_api.cpp

interface_SRCS+=$(_gen_interface_sources)
	
CXXFLAGS += -std=c++11	
interface_CFLAGS=
interface_LDFLAGS=-ldn_common -levent_log
interface_LDFLAGS+=-ldb-api

nas_packet_meta_SRCS=nas_packet_meta.c
nas_packet_meta_CFLAGS=
nas_packet_meta_LDFLAGS=-ldn_common -levent_log

nas_pkt_meta_send_ut_SRCS=unit_test/nas_pkt_meta_send_ut.cpp
nas_pkt_meta_send_ut_CFLAGS=
nas_pkt_meta_send_ut_LDFLAGS=-ldn_common -lnas_packet_meta

nas_pkt_meta_rcv_ut_SRCS=unit_test/nas_pkt_meta_rcv_ut.cpp
nas_pkt_meta_rcv_ut_CFLAGS=
nas_pkt_meta_rcv_ut_LDFLAGS=-ldn_common -lnas_packet_meta

packet_io_SRCS=packet_io.c
packet_io_CFLAGS=
packet_io_LDFLAGS=-ldn_common -lnas_ndi -levent_log -linterface -lnas_packet_meta

test_interface_SRCS=unit_test/tap_main.c
test_interface_CFLAGS=
test_interface_LDFLAGS= -linterface -lnas_ndi

if_cfg_ut_SRCS=interface_file_reader.c unit_test/interface_file_reader_unit_test.c
if_cfg_ut_LDFLAGS=-ldn_common -levent_log

nas_vlan_cps_unittest_SRCS= unit_test/nas_int_vlan_unittest.cpp
nas_vlan_cps_unittest_LDFLAGS= -lhal_common -lcps-api-common -lcps-class-map-util

nas_int_lag_unittest_SRCS = unit_test/nas_int_lag_unittest.cpp
nas_int_lag_unittest_LDFLAGS=-lcps-api-common -lcps-class-map-util -lhal_common

nas_stats_ut_SRCS = unit_test/nas_stats_ut.cpp
nas_stats_ut_LDFLAGS=-lcps-api-common -lcps-class-map-util

nas_int_phy_media_unittest_SRCS=unit_test/nas_int_media_unittest.cpp
nas_int_phy_media_unittest_LDFLAGS=-lcps-api-common -lcps-class-map-util

nas_int_loopback_unittest_SRCS=unit_test/nas_int_loopback_unittest.cpp
nas_int_loopback_unittest_LDFLAGS=-lcps-api-common -lcps-class-map-util

nas_int_rpc_unittest_SRCS=unit_test/nas_int_rpc_unittest.cpp
nas_int_rpc_unittest_LDFLAGS=-lcps-api-common -lcps-class-map-util

include ${MAKE_INC}/workspace.mak
