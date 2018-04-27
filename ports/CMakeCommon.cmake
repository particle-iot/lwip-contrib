set(LWIP_GNU_CLANG_COMMON_FLAGS "-Wall -pedantic -Werror -Wparentheses -Wsequence-point -Wswitch-default -Wextra -Wundef -Wshadow -Wpointer-arith -Wcast-qual -Wc++-compat -Wwrite-strings -Wold-style-definition -Wcast-align -Wmissing-prototypes -Wredundant-decls -Wnested-externs -Wunreachable-code -Wuninitialized -Wmissing-prototypes -Wredundant-decls -Waggregate-return -Wlogical-not-parentheses")

if(CMAKE_C_COMPILER_ID STREQUAL GNU)
    set(CMAKE_C_FLAGS "${LWIP_GNU_CLANG_COMMON_FLAGS} -Wlogical-op -Wc90-c99-compat -Wtrampolines")

    if(NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 4.9)
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fstack-protector -fstack-check -fsanitize=undefined -fno-sanitize=alignment")
    endif()
endif()

if(CMAKE_C_COMPILER_ID STREQUAL Clang)
    set(CMAKE_C_FLAGS "${LWIP_GNU_CLANG_COMMON_FLAGS} -fsanitize=address -fsanitize=undefined -fno-sanitize=alignment -Wdocumentation -Wno-documentation-deprecated-sync")
endif()

if(CMAKE_C_COMPILER_ID STREQUAL MSVC)
    # TODO
endif()

# ARM mbedtls support https://tls.mbed.org/
set(MBEDTLSDIR ${LWIP_CONTRIB_DIR}/../mbedtls)
if(EXISTS ${MBEDTLSDIR}/include/mbedtls/ssl.h)
    set(ENABLE_PROGRAMS OFF CACHE BOOL "Prevent building MBEDTLS programs")

    add_subdirectory(${LWIP_CONTRIB_DIR}/../mbedtls mbedtls)
    add_definitions(-DLWIP_HAVE_MBEDTLS=1)
    include_directories(${MBEDTLSDIR}/include)
    link_libraries(mbedtls mbedcrypto mbedx509)
endif()
