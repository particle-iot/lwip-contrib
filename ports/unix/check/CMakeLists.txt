cmake_minimum_required(VERSION 3.8)

project(lwipunittests C)

if (NOT CMAKE_SYSTEM_NAME STREQUAL Linux AND NOT CMAKE_SYSTEM_NAME STREQUAL Darwin)
    message(FATAL_ERROR "Unit test are currently only working on Linux or Darwin")
endif()

set(LWIP_CONTRIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(LWIP_DIR         ${CMAKE_CURRENT_SOURCE_DIR}/../../../../lwip)

include(${LWIP_CONTRIB_DIR}/ports/CMakeCommon.cmake)

if(CMAKE_C_COMPILER_ID STREQUAL Clang)
    # check.h causes 'error: token pasting of ',' and __VA_ARGS__ is a GNU extension' with clang 9.0.0
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-gnu-zero-variadic-macro-arguments")
endif()

add_definitions(-DLWIP_DEBUG -DLWIP_NOASSERT_ON_ERROR)

include_directories(
    "${LWIP_DIR}/test/unit"
    "${LWIP_DIR}/src/include"
    "${LWIP_CONTRIB_DIR}/"
    "${LWIP_CONTRIB_DIR}/ports/unix/port/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/"
)

include(${LWIP_CONTRIB_DIR}/ports/unix/Filelists.cmake)
include(${LWIP_DIR}/src/Filelists.cmake)
include(${LWIP_DIR}/test/unit/Filelists.cmake)
add_executable(lwip_unittests ${LWIP_TESTFILES})

find_library(LIBCHECK check)
find_library(LIBM m)
target_link_libraries(lwip_unittests lwipallapps lwipcore ${LIBCHECK} ${LIBM})

if (NOT CMAKE_SYSTEM_NAME STREQUAL Darwin)
    # check installed via brew on Darwin doesn't have a separate subunit library (must be statically linked)
    find_library(LIBSUBUNIT subunit)
    target_link_libraries(lwip_unittests ${LIBSUBUNIT})
endif()
