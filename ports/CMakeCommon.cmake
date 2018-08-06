# ARM mbedtls support https://tls.mbed.org/
# Build mbedtls BEFORE adding our own compile flags -
# mbedtls produces errors with them
set(MBEDTLSDIR ${LWIP_CONTRIB_DIR}/../mbedtls)
if(EXISTS ${MBEDTLSDIR}/include/mbedtls/ssl.h)
    set(LWIP_HAVE_MBEDTLS ON BOOL)

    # Prevent building MBEDTLS programs and tests
    set(ENABLE_PROGRAMS OFF CACHE BOOL "")
    set(ENABLE_TESTING  OFF CACHE BOOL "")
    
    # mbedtls uses cmake. Sweet!
    add_subdirectory(${LWIP_CONTRIB_DIR}/../mbedtls mbedtls)
    add_definitions(-DLWIP_HAVE_MBEDTLS=1)
    include_directories(${MBEDTLSDIR}/include)
    link_libraries(mbedtls mbedcrypto mbedx509)
endif()

set(LWIP_COMPILER_FLAGS
    -g
    -Wall
    -pedantic
    -Werror
    -Wparentheses
    -Wsequence-point
    -Wswitch-default
    -Wextra -Wundef
    -Wshadow
    -Wpointer-arith
    -Wcast-qual
    -Wc++-compat
    -Wwrite-strings
    -Wold-style-definition
    -Wcast-align
    -Wmissing-prototypes
    -Wnested-externs
    -Wunreachable-code
    -Wuninitialized
    -Wmissing-prototypes
    -Waggregate-return
    -Wlogical-not-parentheses
    )

if (NOT LWIP_HAVE_MBEDTLS)
    list(APPEND LWIP_COMPILER_FLAGS
        -Wredundant-decls
        )
endif()

if(CMAKE_C_COMPILER_ID STREQUAL GNU)
    list(APPEND LWIP_COMPILER_FLAGS
        -Wlogical-op
        -Wtrampolines
        )
    if (NOT LWIP_HAVE_MBEDTLS)
        list(APPEND LWIP_COMPILER_FLAGS
            -Wc90-c99-compat
            )
    endif()

    if(NOT CMAKE_C_COMPILER_VERSION VERSION_LESS 4.9)
        list(APPEND LWIP_COMPILER_FLAGS
            -fsanitize=address
            -fstack-protector
            -fstack-check
            -fsanitize=undefined
            -fno-sanitize=alignment
            )
    endif()
endif()

if(CMAKE_C_COMPILER_ID STREQUAL Clang)
    list(APPEND LWIP_COMPILER_FLAGS
        -fsanitize=address
        -fsanitize=undefined
        -fno-sanitize=alignment
        -Wdocumentation
        -Wno-documentation-deprecated-sync
        )
endif()

if(CMAKE_C_COMPILER_ID STREQUAL MSVC)
    # TODO
endif()
