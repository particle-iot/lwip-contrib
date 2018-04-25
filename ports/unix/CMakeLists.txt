set(lwipcontribportunix_SRCS
    port/sys_arch.c
    port/perf.c
    port/netif/tapif.c
    port/netif/tunif.c
    port/netif/unixif.c
    port/netif/list.c
    port/netif/tcpdump.c
    port/netif/delif.c
    port/netif/sio.c
    port/netif/fifo.c
)

add_library(lwipcontribportunix EXCLUDE_FROM_ALL ${lwipcontribportunix_SRCS})
