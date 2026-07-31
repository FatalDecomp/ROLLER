# Load and validate the E0-S1 roller-core translation-unit partition.
function(roller_load_core_manifest manifest_path source_root)
    if(NOT EXISTS "${manifest_path}")
        message(FATAL_ERROR "roller-core manifest does not exist: ${manifest_path}")
    endif()

    set(_valid_categories
        EXCLUDE
        STUB_SWAP
        KEEP
        PRESENT_BUT_DORMANT)
    set(_manifest_sources "")
    set(_line_number 0)

    file(STRINGS "${manifest_path}" _manifest_lines)
    foreach(_raw_line IN LISTS _manifest_lines)
        math(EXPR _line_number "${_line_number} + 1")
        string(STRIP "${_raw_line}" _line)
        if(NOT _line STREQUAL "" AND NOT _line MATCHES "^#")
            if(NOT _line MATCHES "^([A-Z_]+)\\|([^|]+)$")
                message(FATAL_ERROR
                    "${manifest_path}:${_line_number}: expected CATEGORY|path")
            endif()
            set(_category "${CMAKE_MATCH_1}")
            set(_relative_path "${CMAKE_MATCH_2}")
            string(STRIP "${_relative_path}" _relative_path)

            list(FIND _valid_categories "${_category}" _category_index)
            if(_category_index EQUAL -1)
                message(FATAL_ERROR
                    "${manifest_path}:${_line_number}: unknown category ${_category}")
            endif()
            if(NOT _relative_path MATCHES "^PROJECTS/ROLLER/[^/]+\\.c$")
                message(FATAL_ERROR
                    "${manifest_path}:${_line_number}: invalid source path ${_relative_path}")
            endif()
            list(FIND _manifest_sources "${_relative_path}" _source_index)
            if(NOT _source_index EQUAL -1)
                message(FATAL_ERROR
                    "${manifest_path}:${_line_number}: duplicate source ${_relative_path}")
            endif()
            if(NOT EXISTS "${source_root}/${_relative_path}")
                message(FATAL_ERROR
                    "${manifest_path}:${_line_number}: source does not exist: ${_relative_path}")
            endif()

            list(APPEND _manifest_sources "${_relative_path}")
            list(APPEND _${_category}_sources "${source_root}/${_relative_path}")
        endif()
    endforeach()

    file(GLOB _actual_sources
        RELATIVE "${source_root}"
        "${source_root}/PROJECTS/ROLLER/*.c")
    set(_unclassified_sources ${_actual_sources})
    list(REMOVE_ITEM _unclassified_sources ${_manifest_sources})
    set(_stale_sources ${_manifest_sources})
    list(REMOVE_ITEM _stale_sources ${_actual_sources})
    if(_unclassified_sources)
        message(FATAL_ERROR
            "roller-core manifest has unclassified translation units: ${_unclassified_sources}")
    endif()
    if(_stale_sources)
        message(FATAL_ERROR
            "roller-core manifest names missing translation units: ${_stale_sources}")
    endif()

    foreach(_stub_source ${_STUB_SWAP_sources})
        string(REGEX REPLACE "_stub\\.c$" ".c" _real_source "${_stub_source}")
        if("${_real_source}" STREQUAL "${_stub_source}")
            message(FATAL_ERROR
                "roller-core STUB_SWAP source lacks _stub suffix: ${_stub_source}")
        endif()
        list(FIND _EXCLUDE_sources "${_real_source}" _real_source_index)
        if(_real_source_index EQUAL -1)
            message(FATAL_ERROR
                "roller-core STUB_SWAP source ${_stub_source} does not replace an EXCLUDE source")
        endif()
    endforeach()

    set(ROLLER_CORE_EXCLUDE_SOURCES "${_EXCLUDE_sources}" PARENT_SCOPE)
    set(ROLLER_CORE_STUB_SWAP_SOURCES "${_STUB_SWAP_sources}" PARENT_SCOPE)
    set(ROLLER_CORE_KEEP_SOURCES "${_KEEP_sources}" PARENT_SCOPE)
    set(ROLLER_CORE_PRESENT_BUT_DORMANT_SOURCES
        "${_PRESENT_BUT_DORMANT_sources}" PARENT_SCOPE)
    set(ROLLER_CORE_SOURCES
        ${_KEEP_sources}
        ${_PRESENT_BUT_DORMANT_sources}
        ${_STUB_SWAP_sources}
        PARENT_SCOPE)
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
    if(NOT DEFINED ROLLER_SOURCE_DIR)
        message(FATAL_ERROR "ROLLER_SOURCE_DIR is required in script mode")
    endif()
    roller_load_core_manifest(
        "${ROLLER_SOURCE_DIR}/roller-core.srclist"
        "${ROLLER_SOURCE_DIR}")
    list(LENGTH ROLLER_CORE_SOURCES _core_source_count)
    message(STATUS
        "roller-core manifest CMake check passed: ${_core_source_count} core sources")
endif()
