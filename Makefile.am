bin_SCRIPTS = scripts/bin/*

pyutilsdir=$(libdir)/sonic
pyutils_SCRIPTS = scripts/lib/python/*.py

cfgdir = $(sysconfdir)/sonic
cfg_SCRIPTS = scripts/*.xml

nobase_include_HEADERS=sonic/hal_interface.h sonic/nas_packet_meta.h

lib_LTLIBRARIES=libsonic_nas_interface.la libsonic_nas_meta_packet.la libsonic_nas_packet_io.la

AM_CPPFLAGS=-D_FILE_OFFSET_BITS=64 -I$(top_srcdir)/sonic -I$(includedir)/sonic
AM_CXXFLAGS=-std=c++11
AM_LDFLAGS=-shared -version-info 1:1:0

libsonic_nas_interface_la_SOURCES=src/swp_util_tap.c src/nas_int_main.cpp \
         src/nas_int_common_obj.cpp src/nas_int_list.c \
         src/nas_int_ev_handlers.cpp src/nas_int_bridge.cpp \
         src/nas_int_vlan.cpp src/nas_vlan_cps.cpp \
         src/lag/nas_int_lag.c src/lag/nas_int_lag_api.cpp \
         src/lag/nas_int_lag_cps.cpp src/port/hal_int_utils.c \
         src/port/nas_int_logical_cps.cpp src/port/nas_int_port.cpp \
         src/port/nas_int_breakout.cpp src/port/nas_int_physical_cps.cpp \
         src/stats/nas_stats_if_cps.cpp src/stats/nas_stats_vlan_cps.cpp \
         src/nas_vlan_lag_api.cpp
libsonic_nas_interface_la_LIBADD=-lsonic_common -lsonic_nas_common -lsonic_nas_ndi -lsonic_object_library -lsonic_logging -lsonic_nas_linux -lsonic_nas_platform -lpthread

libsonic_nas_meta_packet_la_SOURCES=src/nas_packet_meta.c
libsonic_nas_meta_packet_la_LIBADD=-lsonic_common -lsonic_logging

libsonic_nas_packet_io_la_SOURCES=src/packet_io.c
libsonic_nas_packet_io_la_LIBADD=-lsonic_common -lsonic_logging -lsonic_nas_interface -lsonic_nas_meta_packet -lsonic_nas_ndi -lsonic_object_library -lpthread

systemdconfdir=/lib/systemd/system
systemdconf_DATA = scripts/init/*.service
