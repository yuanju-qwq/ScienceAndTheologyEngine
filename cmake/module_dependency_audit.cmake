# Engine module dependency audit.
#
# The engine deliberately keeps its existing "module/header.h" include layout
# instead of creating one physical include root per module. Consequently, a
# compiler can find a sibling header even when the consuming CMake target did
# not declare the owning library. This file makes that relationship explicit:
# registered modules export the engine source root themselves, and configure
# time verification rejects every quoted internal include whose owner is not a
# direct target_link_libraries dependency.

include_guard(GLOBAL)

function(snt_register_engine_module)
    cmake_parse_arguments(SNT_MODULE
        "INTERFACE"
        "TARGET"
        "INCLUDE_PREFIXES;SOURCE_DIRS;SOURCE_FILES"
        ${ARGN}
    )

    if(NOT SNT_MODULE_TARGET OR NOT TARGET ${SNT_MODULE_TARGET})
        message(FATAL_ERROR "snt_register_engine_module requires an existing TARGET.")
    endif()

    if(NOT SNT_MODULE_INCLUDE_PREFIXES)
        message(FATAL_ERROR
            "snt_register_engine_module(${SNT_MODULE_TARGET}) requires INCLUDE_PREFIXES."
        )
    endif()

    if(NOT SNT_MODULE_SOURCE_DIRS AND NOT SNT_MODULE_SOURCE_FILES)
        message(FATAL_ERROR
            "snt_register_engine_module(${SNT_MODULE_TARGET}) requires SOURCE_DIRS or SOURCE_FILES."
        )
    endif()

    # The source root belongs to each module's public build interface. It is
    # intentionally not inherited from snt_engine_settings, which must contain
    # compiler settings only. The audit below enforces that a visible header is
    # also backed by a direct CMake dependency.
    if(SNT_MODULE_INTERFACE)
        target_include_directories(${SNT_MODULE_TARGET} INTERFACE
            "$<BUILD_INTERFACE:${SNT_ENGINE_SOURCE_DIR}>"
        )
    else()
        target_include_directories(${SNT_MODULE_TARGET} PUBLIC
            "$<BUILD_INTERFACE:${SNT_ENGINE_SOURCE_DIR}>"
        )
    endif()

    set_property(TARGET ${SNT_MODULE_TARGET} PROPERTY
        SNT_AUDIT_SOURCE_DIRS "${SNT_MODULE_SOURCE_DIRS}"
    )
    set_property(TARGET ${SNT_MODULE_TARGET} PROPERTY
        SNT_AUDIT_SOURCE_FILES "${SNT_MODULE_SOURCE_FILES}"
    )
    set_property(GLOBAL APPEND PROPERTY SNT_ENGINE_AUDIT_TARGETS
        ${SNT_MODULE_TARGET}
    )

    foreach(_prefix IN LISTS SNT_MODULE_INCLUDE_PREFIXES)
        set_property(GLOBAL APPEND PROPERTY SNT_ENGINE_INCLUDE_MAPPINGS
            "${_prefix}|${SNT_MODULE_TARGET}"
        )
    endforeach()
endfunction()

function(_snt_find_module_owner include_path out_target)
    get_property(_mappings GLOBAL PROPERTY SNT_ENGINE_INCLUDE_MAPPINGS)
    set(_best_target "")
    set(_best_prefix_length -1)
    string(LENGTH "${include_path}" _include_length)

    foreach(_mapping IN LISTS _mappings)
        string(REPLACE "|" ";" _parts "${_mapping}")
        list(GET _parts 0 _prefix)
        list(GET _parts 1 _target)

        string(LENGTH "${_prefix}" _prefix_length)
        if(_include_length LESS _prefix_length)
            continue()
        endif()

        string(SUBSTRING "${include_path}" 0 ${_prefix_length} _candidate)
        if(NOT _candidate STREQUAL _prefix)
            continue()
        endif()

        set(_prefix_matches FALSE)
        if(_include_length EQUAL _prefix_length)
            set(_prefix_matches TRUE)
        else()
            string(SUBSTRING "${include_path}" ${_prefix_length} 1 _separator)
            if(_separator STREQUAL "/")
                set(_prefix_matches TRUE)
            endif()
        endif()

        if(_prefix_matches AND _prefix_length GREATER _best_prefix_length)
            set(_best_target "${_target}")
            set(_best_prefix_length ${_prefix_length})
        endif()
    endforeach()

    set(${out_target} "${_best_target}" PARENT_SCOPE)
endfunction()

function(_snt_collect_direct_dependencies target out_dependencies)
    set(_dependencies)

    get_target_property(_target_type ${target} TYPE)
    if(_target_type STREQUAL "INTERFACE_LIBRARY")
        set(_properties INTERFACE_LINK_LIBRARIES)
    else()
        # A compiled target must consume an implementation dependency through
        # PUBLIC or PRIVATE. An INTERFACE-only edge cannot satisfy its sources.
        set(_properties LINK_LIBRARIES)
    endif()

    foreach(_property IN LISTS _properties)
        get_target_property(_entries ${target} ${_property})
        if(_entries STREQUAL "_entries-NOTFOUND")
            continue()
        endif()

        foreach(_entry IN LISTS _entries)
            # CMake wraps dependencies added from another directory under
            # CMP0079. The wrapper is metadata, not a link target.
            if(_entry MATCHES "^::@")
                continue()
            endif()
            list(APPEND _dependencies "${_entry}")
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES _dependencies)
    set(${out_dependencies} "${_dependencies}" PARENT_SCOPE)
endfunction()

function(snt_verify_engine_module_dependencies)
    get_property(_targets GLOBAL PROPERTY SNT_ENGINE_AUDIT_TARGETS)
    list(REMOVE_DUPLICATES _targets)

    set(_violations)
    set(_edges)

    foreach(_target IN LISTS _targets)
        get_target_property(_source_dirs ${_target} SNT_AUDIT_SOURCE_DIRS)
        get_target_property(_source_files ${_target} SNT_AUDIT_SOURCE_FILES)
        set(_files ${_source_files})

        foreach(_source_dir IN LISTS _source_dirs)
            if(NOT IS_DIRECTORY "${_source_dir}")
                message(FATAL_ERROR
                    "SNT dependency audit source directory does not exist for ${_target}: ${_source_dir}"
                )
            endif()

            file(GLOB_RECURSE _directory_files CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
                "${_source_dir}/*.h"
                "${_source_dir}/*.hpp"
                "${_source_dir}/*.inl"
                "${_source_dir}/*.cpp"
                "${_source_dir}/*.cxx"
            )
            list(APPEND _files ${_directory_files})
        endforeach()

        list(REMOVE_DUPLICATES _files)
        _snt_collect_direct_dependencies(${_target} _direct_dependencies)

        foreach(_file IN LISTS _files)
            if(NOT EXISTS "${_file}")
                message(FATAL_ERROR
                    "SNT dependency audit source file does not exist for ${_target}: ${_file}"
                )
            endif()

            file(READ "${_file}" _contents)
            string(REGEX MATCHALL "#[ \t]*include[ \t]*\"[^\"]+\"" _include_lines "${_contents}")

            foreach(_include_line IN LISTS _include_lines)
                string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" _include_path "${_include_line}")
                _snt_find_module_owner("${_include_path}" _owner)

                if(NOT _owner OR _owner STREQUAL _target)
                    continue()
                endif()

                list(APPEND _edges "${_target}|${_owner}|${_include_path}")
                list(FIND _direct_dependencies "${_owner}" _dependency_index)
                if(_dependency_index EQUAL -1)
                    file(RELATIVE_PATH _relative_file "${SNT_ENGINE_SOURCE_DIR}" "${_file}")
                    list(APPEND _violations
                        "${_target}: ${_relative_file} includes \"${_include_path}\" but does not directly link ${_owner}"
                    )
                endif()
            endforeach()
        endforeach()
    endforeach()

    list(REMOVE_DUPLICATES _edges)
    list(REMOVE_DUPLICATES _violations)
    if(_violations)
        list(JOIN _violations "\n  " _formatted_violations)
        message(FATAL_ERROR
            "SNT module dependency audit failed. Add each owning module directly "
            "with target_link_libraries (PUBLIC for public-header dependencies, "
            "PRIVATE for implementation-only dependencies):\n  ${_formatted_violations}"
        )
    endif()

    list(LENGTH _targets _target_count)
    list(LENGTH _edges _edge_count)
    message(STATUS
        "SNT module dependency audit passed: ${_target_count} targets, ${_edge_count} internal include edges."
    )
endfunction()
