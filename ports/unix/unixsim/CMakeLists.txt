cmake_minimum_required(VERSION 3.8)

project(unixsim C)

set(LWIP_CONTRIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(LWIP_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/../../../../lwip)

include(${LWIP_CONTRIB_DIR}/ports/CMakeCommon.cmake)

add_definitions(-DLWIP_DEBUG)

include_directories(
    "${LWIP_CONTRIB_DIR}/../lwip/src/include"
    "${LWIP_CONTRIB_DIR}/"
    "${LWIP_CONTRIB_DIR}/ports/unix/port/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/./"
)

add_subdirectory(../../../../lwip/src lwip)
add_subdirectory(../../../ contrib)
add_subdirectory(../ unixport)

# lwipcontribaddons is specified multiple times in target_link_libraries because of cyclic dependency
# lwipcontribaddons call various functions in lwipcore
# lwipcore needs tcp_hook_isn from lwipcontribaddons

add_executable(simhost simhost.c)
target_link_libraries(simhost lwipcontribexamples lwipcontribapps lwipcontribaddons lwipapps lwipcontribportunix lwipcore lwipcontribaddons)

add_executable(simnode simnode.c)
target_link_libraries(simnode lwipcontribexamples lwipcontribapps lwipcontribaddons lwipapps lwipcontribportunix lwipcore lwipcontribaddons)

add_executable(simrouter simrouter.c)
target_link_libraries(simrouter lwipcontribexamples lwipcontribapps lwipcontribaddons lwipapps lwipcontribportunix lwipcore lwipcontribaddons)
