cmake_minimum_required(VERSION 3.8)

include_directories(lwip PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../lwip/src/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../"
    "${CMAKE_CURRENT_SOURCE_DIR}/../port/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/./"
)

add_subdirectory(../../../ contrib)
add_subdirectory(../../../../lwip/src lwip)
add_subdirectory(../ unixport)

link_libraries(pthread)

add_executable(simhost simhost.c)
target_link_libraries(simhost lwipcontribportunix lwipcontribexamples lwipcontribapps lwipcontribaddons lwip)

add_executable(simnode simnode.c)
target_link_libraries(simnode lwipcontribportunix lwipcontribexamples lwipcontribapps lwipcontribaddons lwip)

add_executable(simrouter simrouter.c)
target_link_libraries(simrouter lwipcontribportunix lwipcontribexamples lwipcontribapps lwipcontribaddons lwip)
