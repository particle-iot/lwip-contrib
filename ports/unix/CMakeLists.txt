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

if (CMAKE_SYSTEM_NAME STREQUAL Linux)
    find_library(LIBUTIL util)
    link_libraries(${LIBUTIL})
    
    find_library(LIBPTHREAD pthread)
    link_libraries(${LIBPTHREAD})
    
    find_library(LIBRT rt)
    link_libraries(${LIBRT})
endif()

if (CMAKE_SYSTEM_NAME STREQUAL Darwin)
    # Darwin doesn't have pthreads or POSIX real-time extensions libs
    find_library(LIBUTIL util)
    link_libraries(${LIBUTIL})
endif()

add_library(lwipcontribportunix EXCLUDE_FROM_ALL ${lwipcontribportunix_SRCS})
