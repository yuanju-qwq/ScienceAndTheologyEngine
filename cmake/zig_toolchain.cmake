# Locked Zig toolchain integration for native engine modules.
#
# Zig modules expose only C ABI symbols to the C++ engine. The CMake target is
# an interface wrapper around a Zig-produced static archive so C++ consumers
# receive the archive and its build dependency without learning Zig types.

include_guard(GLOBAL)

set(SNT_ZIG_REQUIRED_VERSION "0.16.0-dev.3142+5ccfeb926")
set(SNT_ZIG_EXECUTABLE "" CACHE FILEPATH
    "Path to the locked Zig compiler used by SNT native engine modules")
set(SNT_ZIG_TARGET "" CACHE STRING
    "Optional Zig target triple; leave empty to match the local CMake host")

function(snt_require_zig_toolchain)
    if(NOT SNT_ZIG_EXECUTABLE)
        find_program(_snt_zig_candidate NAMES zig)
        if(_snt_zig_candidate)
            set(SNT_ZIG_EXECUTABLE "${_snt_zig_candidate}" CACHE FILEPATH
                "Path to the locked Zig compiler used by SNT native engine modules" FORCE)
        endif()
    endif()

    if(NOT SNT_ZIG_EXECUTABLE OR NOT EXISTS "${SNT_ZIG_EXECUTABLE}")
        message(FATAL_ERROR
            "SNT native Zig modules require Zig ${SNT_ZIG_REQUIRED_VERSION}. "
            "Set -DSNT_ZIG_EXECUTABLE=/absolute/path/to/zig.")
    endif()

    execute_process(
        COMMAND "${SNT_ZIG_EXECUTABLE}" version
        RESULT_VARIABLE _snt_zig_version_result
        OUTPUT_VARIABLE _snt_zig_version
        ERROR_VARIABLE _snt_zig_version_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _snt_zig_version_result EQUAL 0)
        message(FATAL_ERROR
            "Unable to execute SNT_ZIG_EXECUTABLE '${SNT_ZIG_EXECUTABLE}': "
            "${_snt_zig_version_error}")
    endif()
    if(NOT _snt_zig_version STREQUAL SNT_ZIG_REQUIRED_VERSION)
        message(FATAL_ERROR
            "SNT native Zig modules require Zig ${SNT_ZIG_REQUIRED_VERSION}, "
            "but '${SNT_ZIG_EXECUTABLE}' reports ${_snt_zig_version}.")
    endif()

    set(_snt_zig_effective_target "${SNT_ZIG_TARGET}")
    if(NOT _snt_zig_effective_target AND WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_snt_zig_effective_target "x86_64-windows-msvc")
        else()
            set(_snt_zig_effective_target "x86-windows-msvc")
        endif()
    endif()
    set(SNT_ZIG_EFFECTIVE_TARGET "${_snt_zig_effective_target}" PARENT_SCOPE)
endfunction()

function(snt_add_zig_static_library)
    cmake_parse_arguments(SNT_ZIG_LIBRARY "" "TARGET" "SOURCES;INCLUDE_DIRECTORIES;DEPENDS" ${ARGN})

    if(NOT SNT_ZIG_LIBRARY_TARGET OR NOT SNT_ZIG_LIBRARY_SOURCES)
        message(FATAL_ERROR
            "snt_add_zig_static_library requires TARGET and SOURCES.")
    endif()
    if(TARGET ${SNT_ZIG_LIBRARY_TARGET})
        message(FATAL_ERROR
            "snt_add_zig_static_library target already exists: ${SNT_ZIG_LIBRARY_TARGET}")
    endif()

    if(WIN32)
        set(_snt_zig_archive_suffix ".lib")
    else()
        set(_snt_zig_archive_suffix ".a")
    endif()

    set(_snt_zig_output_directory "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>")
    set(_snt_zig_archive
        "${_snt_zig_output_directory}/${SNT_ZIG_LIBRARY_TARGET}${_snt_zig_archive_suffix}")
    set(_snt_zig_cache_directory
        "${CMAKE_CURRENT_BINARY_DIR}/zig-cache/$<CONFIG>")
    set(_snt_zig_global_cache_directory "${CMAKE_BINARY_DIR}/zig-global-cache")
    # A Zig Debug archive brings Zig's panic/runtime support into the MSVC
    # final link. ABI entry points validate all external arguments themselves,
    # so use a runtime-free ReleaseFast archive in every CMake configuration;
    # the separate `zig test -O Debug` contract test retains safety coverage.
    set(_snt_zig_optimize "ReleaseFast")
    set(_snt_zig_target_arguments)
    if(SNT_ZIG_EFFECTIVE_TARGET)
        list(APPEND _snt_zig_target_arguments -target "${SNT_ZIG_EFFECTIVE_TARGET}")
    endif()
    set(_snt_zig_include_arguments)
    foreach(_snt_zig_include_directory IN LISTS SNT_ZIG_LIBRARY_INCLUDE_DIRECTORIES)
        list(APPEND _snt_zig_include_arguments -I "${_snt_zig_include_directory}")
    endforeach()
    set(_snt_zig_callsite_file "${CMAKE_CURRENT_LIST_FILE}")

    add_custom_command(
        OUTPUT "${_snt_zig_archive}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_snt_zig_output_directory}"
        COMMAND "${SNT_ZIG_EXECUTABLE}" build-lib
            -static
            --name "${SNT_ZIG_LIBRARY_TARGET}"
            -O "${_snt_zig_optimize}"
            ${_snt_zig_target_arguments}
            ${_snt_zig_include_arguments}
            --cache-dir "${_snt_zig_cache_directory}"
            --global-cache-dir "${_snt_zig_global_cache_directory}"
            "-femit-bin=${_snt_zig_archive}"
            ${SNT_ZIG_LIBRARY_SOURCES}
        DEPENDS
            ${SNT_ZIG_LIBRARY_SOURCES}
            "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
            "${_snt_zig_callsite_file}"
            ${SNT_ZIG_LIBRARY_DEPENDS}
        COMMENT "Building Zig static library ${SNT_ZIG_LIBRARY_TARGET}"
        VERBATIM
    )

    add_custom_target(${SNT_ZIG_LIBRARY_TARGET}_build
        DEPENDS "${_snt_zig_archive}"
    )

    # Interface targets can carry a generated archive and ensure that a C++
    # consumer builds it before link. The archive itself remains a real static
    # library produced by Zig, not a CMake fallback implementation.
    add_library(${SNT_ZIG_LIBRARY_TARGET} INTERFACE)
    add_dependencies(${SNT_ZIG_LIBRARY_TARGET} ${SNT_ZIG_LIBRARY_TARGET}_build)
    target_link_libraries(${SNT_ZIG_LIBRARY_TARGET} INTERFACE "${_snt_zig_archive}")
    set_property(TARGET ${SNT_ZIG_LIBRARY_TARGET} PROPERTY
        SNT_ZIG_ARCHIVE "${_snt_zig_archive}")
endfunction()
