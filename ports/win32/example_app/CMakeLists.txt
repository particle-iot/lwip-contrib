cmake_minimum_required(VERSION 3.8)

project(example_app C)

set(LWIP_CONTRIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(LWIP_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/../../../../lwip)

include(${LWIP_CONTRIB_DIR}/ports/CMakeCommon.cmake)

include(${LWIP_DIR}/src/Filelists.cmake)
include(${LWIP_CONTRIB_DIR}/Filelists.cmake)
include(${LWIP_CONTRIB_DIR}/ports/win32/Filelists.cmake)

add_definitions(-DLWIP_DEBUG)

include_directories(
    "${LWIP_DIR}/src/include"
    "${LWIP_CONTRIB_DIR}/"
    "${LWIP_CONTRIB_DIR}/ports/win32/include"
    "${LWIP_CONTRIB_DIR}/examples/example_app"
)

add_executable(example_app ${LWIP_CONTRIB_DIR}/examples/example_app/test.c default_netif.c)
target_link_libraries(example_app lwipcontribexamples lwipcontribapps lwipcontribaddons lwipallapps lwipcontribportwindows lwipcore)

add_executable(makefsdata ${lwipmakefsdata_SRCS})
