cmake_minimum_required(VERSION 3.8)

project(lwip C)

set(LWIP_CONTRIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(LWIP_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/../../../../lwip)

include(${LWIP_CONTRIB_DIR}/ports/CMakeCommon.cmake)
include(${LWIP_DIR}/src/Filelists.cmake)
include(${LWIP_CONTRIB_DIR}/Filelists.cmake)
include(${LWIP_CONTRIB_DIR}/ports/win32/Filelists.cmake)

add_definitions(-DLWIP_DEBUG)

include_directories(
    "${LWIP_CONTRIB_DIR}/../lwip/src/include"
    "${LWIP_CONTRIB_DIR}/"
    "${LWIP_CONTRIB_DIR}/ports/win32/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/"
)

add_executable(lwiptest ../test.c)
target_link_libraries(lwiptest lwipcontribexamples lwipcontribapps lwipcontribaddons lwipcore lwipallapps lwipcontribportwindows)
