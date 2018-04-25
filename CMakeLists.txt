set(lwipcontribexamples_SRCS
    examples/httpd/fs_example/fs_example.c
    examples/httpd/ssi_example/ssi_example.c
    examples/lwiperf/lwiperf_example.c
    examples/mdns/mdns_example.c
    examples/mqtt/mqtt_example.c
    examples/ppp/pppos_example.c
    examples/snmp/snmp_private_mib/lwip_prvmib.c
    examples/snmp/snmp_v3/snmpv3_dummy.c
    examples/snmp/snmp_example.c
    examples/sntp/sntp_example.c
    examples/tftp/tftp_example.c
)
add_library(lwipcontribexamples EXCLUDE_FROM_ALL ${lwipcontribexamples_SRCS})

set(lwipcontribapps_SRCS
    apps/httpserver/httpserver-netconn.c
	apps/chargen/chargen.c
	apps/udpecho/udpecho.c
	apps/tcpecho/tcpecho.c
	apps/shell/shell.c
	apps/udpecho_raw/udpecho_raw.c
	apps/tcpecho_raw/tcpecho_raw.c
	apps/netio/netio.c
	apps/ping/ping.c
	apps/socket_examples/socket_examples.c
    apps/rtp/rtp.c
)
add_library(lwipcontribapps EXCLUDE_FROM_ALL ${lwipcontribapps_SRCS})

set(lwipcontribaddons_SRCS
    addons/tcp_isn/tcp_isn.c
    addons/ipv6_static_routing/ip6_route_table.c
)
add_library(lwipcontribaddons EXCLUDE_FROM_ALL ${lwipcontribaddons_SRCS})
