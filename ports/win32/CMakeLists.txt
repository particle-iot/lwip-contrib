set(lwipcontribportwindows_SRCS
    sys_arch.c
    sio.c
    pcapif.c
    pcapif_helper.c
)

# pcapif needs WinPcap developer package: https://www.winpcap.org/devel.htm
set(WPDPACK_DIR ${LWIP_CONTRIB_DIR}/../WpdPack)
find_library(WPCAP  wpcap  HINTS ${WPDPACK_DIR}/lib)
find_library(PACKET packet HINTS ${WPDPACK_DIR}/lib)
include_directories("${WPDPACK_DIR}/include")
link_libraries(${WPCAP} ${PACKET})

add_library(lwipcontribportwindows EXCLUDE_FROM_ALL ${lwipcontribportwindows_SRCS})
