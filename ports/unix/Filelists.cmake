# This file is indended to be included in end-user CMakeLists.txt
# include(/path/to/Filelists.cmake)
# It assumes the variable LWIP_CONTRIB_DIR is defined pointing to the
# root path of lwIP contrib sources.
#
# This file is NOT designed (on purpose) to be included as cmake
# subdir via add_subdirectory()
# The intention is to provide greater flexibility to users to 
# create their own targets using the *_SRCS variables.

set(lwipcontribportunix_SRCS
    ${LWIP_CONTRIB_DIR}/ports/unix/port/sys_arch.c
    ${LWIP_CONTRIB_DIR}/ports/unix/port/perf.c
)

set(lwipcontribportunixnetifs_SRCS
    ${LWIP_CONTRIB_DIR}/ports/unix/port/netif/tapif.c
    ${LWIP_CONTRIB_DIR}/ports/unix/port/netif/tunif.c
    ${LWIP_CONTRIB_DIR}/ports/unix/port/netif/unixif.c
    ${LWIP_CONTRIB_DIR}/ports/unix/port/netif/list.c
    ${LWIP_CONTRIB_DIR}/ports/unix/port/netif/tcpdump.c
    ${LWIP_CONTRIB_DIR}/ports/unix/port/netif/delif.c
    ${LWIP_CONTRIB_DIR}/ports/unix/port/netif/sio.c
    ${LWIP_CONTRIB_DIR}/ports/unix/port/netif/fifo.c
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

add_library(lwipcontribportunix EXCLUDE_FROM_ALL ${lwipcontribportunix_SRCS} ${lwipcontribportunixnetifs_SRCS})
