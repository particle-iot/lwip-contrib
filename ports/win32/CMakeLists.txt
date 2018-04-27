set(lwipcontribportwindows_SRCS
    sys_arch.c
    sio.c
    pcapif.c
    pcapif_helper.c
)

# pcapif needs WinPcap developer package: https://www.winpcap.org/devel.htm
find_library(WPCAP  wpcap  HINTS ${LWIP_CONTRIB_DIR}/../WpdPack/lib)
find_library(PACKET packet HINTS ${LWIP_CONTRIB_DIR}/../WpdPack/lib)
include_directories("${LWIP_CONTRIB_DIR}/../WpdPack/include")
link_libraries(${WPCAP} ${PACKET})

add_library(lwipcontribportwindows EXCLUDE_FROM_ALL ${lwipcontribportwindows_SRCS})
