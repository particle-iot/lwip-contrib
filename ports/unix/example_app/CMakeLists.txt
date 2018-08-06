cmake_minimum_required(VERSION 3.8)

project(example_app C)

set (CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LWIP_CONTRIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(LWIP_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/../../../../lwip)

include(${LWIP_CONTRIB_DIR}/ports/CMakeCommon.cmake)

include(${LWIP_DIR}/src/Filelists.cmake)
include(${LWIP_CONTRIB_DIR}/Filelists.cmake)
include(${LWIP_CONTRIB_DIR}/ports/unix/Filelists.cmake)

add_definitions(-DLWIP_DEBUG)

set (LWIP_INCLUDE_DIRS
    "${LWIP_DIR}/src/include"
    "${LWIP_CONTRIB_DIR}/"
    "${LWIP_CONTRIB_DIR}/ports/unix/port/include"
    "${LWIP_CONTRIB_DIR}/examples/example_app"
)

add_executable(example_app ${LWIP_CONTRIB_DIR}/examples/example_app/test.c default_netif.c)
target_link_libraries(example_app lwipcontribexamples lwipcontribapps lwipcontribaddons lwipallapps lwipcontribportunix lwipcore)

add_executable(makefsdata ${lwipmakefsdata_SRCS})

target_include_directories(example_app PRIVATE ${LWIP_INCLUDE_DIRS})
target_include_directories(makefsdata PRIVATE ${LWIP_INCLUDE_DIRS})

target_include_directories(lwipcore PRIVATE ${LWIP_INCLUDE_DIRS})
target_include_directories(lwipallapps PRIVATE ${LWIP_INCLUDE_DIRS})
target_include_directories(lwipcontribaddons PRIVATE ${LWIP_INCLUDE_DIRS})
target_include_directories(lwipcontribapps PRIVATE ${LWIP_INCLUDE_DIRS})
target_include_directories(lwipcontribexamples PRIVATE ${LWIP_INCLUDE_DIRS})
target_include_directories(lwipcontribportunix PRIVATE ${LWIP_INCLUDE_DIRS})
