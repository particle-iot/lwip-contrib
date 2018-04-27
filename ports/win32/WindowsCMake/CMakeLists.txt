cmake_minimum_required(VERSION 3.8)

project(lwip C)

set(LWIP_CONTRIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(LWIP_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/../../../../lwip)

include(${LWIP_CONTRIB_DIR}/ports/CMakeCommon.cmake)

add_definitions(-DLWIP_DEBUG)

include_directories(
    "${LWIP_CONTRIB_DIR}/../lwip/src/include"
    "${LWIP_CONTRIB_DIR}/"
    "${LWIP_CONTRIB_DIR}/ports/win32/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/"
)

add_subdirectory(../ win32port)
link_libraries(lwipcontribportwindows)

add_subdirectory(${LWIP_CONTRIB_DIR}/../lwip/src lwip)
add_subdirectory(${LWIP_CONTRIB_DIR}/ contrib)

add_executable(lwiptest ../test.c)
target_link_libraries(lwiptest lwipcontribexamples lwipcontribapps lwipcontribaddons lwipcore lwipapps)
