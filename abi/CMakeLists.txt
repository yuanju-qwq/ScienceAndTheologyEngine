# Language-neutral ABI boundary.
#
# snt_abi is the one static archive that native consumers link. Zig builds it
# from its own implementation leaves plus the small C descriptor source, so it
# stays free of C++ runtime, ECS, GPU, allocator, and exception dependencies.

include("${SNT_ENGINE_SOURCE_DIR}/cmake/zig_toolchain.cmake")
snt_require_zig_toolchain()

snt_add_zig_static_library(
    TARGET snt_abi
    SOURCES
        "${SNT_ENGINE_SOURCE_DIR}/zig/hash.zig"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime_abi.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/runtime_host_abi.c"
    INCLUDE_DIRECTORIES
        "${SNT_ENGINE_SOURCE_DIR}"
    DEPENDS
        "${CMAKE_CURRENT_LIST_FILE}"
        "${SNT_ENGINE_SOURCE_DIR}/abi/abi_common.h"
        "${SNT_ENGINE_SOURCE_DIR}/abi/hash_abi.h"
        "${SNT_ENGINE_SOURCE_DIR}/abi/render_snapshot_abi.h"
        "${SNT_ENGINE_SOURCE_DIR}/abi/runtime_abi.h"
        "${SNT_ENGINE_SOURCE_DIR}/abi/runtime_host_abi.h"
)

get_property(_snt_abi_archive TARGET snt_abi PROPERTY SNT_ZIG_ARCHIVE)
if(SNT_BUILD_TESTS)
    set(_snt_zig_test_target_arguments)
    if(SNT_ZIG_EFFECTIVE_TARGET)
        list(APPEND _snt_zig_test_target_arguments -target "${SNT_ZIG_EFFECTIVE_TARGET}")
    endif()

    add_test(
        NAME ZigHash.NativeContract
        COMMAND "${SNT_ZIG_EXECUTABLE}" test "${SNT_ENGINE_SOURCE_DIR}/zig/hash.zig"
            -O Debug
            ${_snt_zig_test_target_arguments}
            --cache-dir "${CMAKE_CURRENT_BINARY_DIR}/zig-hash-test-cache"
            --global-cache-dir "${CMAKE_BINARY_DIR}/zig-global-cache"
    )
    add_test(
        NAME ZigAbi.ConsumerStaticLink
        COMMAND "${SNT_ZIG_EXECUTABLE}" test
            -O Debug
            ${_snt_zig_test_target_arguments}
            -I "${SNT_ENGINE_SOURCE_DIR}"
            --cache-dir "${CMAKE_CURRENT_BINARY_DIR}/zig-consumer-test-cache/$<CONFIG>"
            --global-cache-dir "${CMAKE_BINARY_DIR}/zig-global-cache"
            "${SNT_ENGINE_SOURCE_DIR}/zig/abi_consumer_smoke.zig"
            -lc
            "${_snt_abi_archive}"
    )
    set_tests_properties(ZigHash.NativeContract PROPERTIES
        ENVIRONMENT
            "ZIG_GLOBAL_CACHE_DIR=${CMAKE_BINARY_DIR}/zig-global-cache;ZIG_LOCAL_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/zig-hash-test-cache"
    )
    set_tests_properties(ZigAbi.ConsumerStaticLink PROPERTIES
        ENVIRONMENT
            "ZIG_GLOBAL_CACHE_DIR=${CMAKE_BINARY_DIR}/zig-global-cache;ZIG_LOCAL_CACHE_DIR=${CMAKE_CURRENT_BINARY_DIR}/zig-consumer-test-cache"
    )
endif()
