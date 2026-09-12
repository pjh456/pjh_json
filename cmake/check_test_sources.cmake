# cmake/check_test_sources.cmake
#
# Configure-time guard (roadmap 66): every top-level tests/*.cpp MUST be a
# registered test source. tests/CMakeLists.txt lists doctest TUs explicitly in
# PJH_JSON_TEST_SOURCES (no glob; AGENTS.md rule), so a test file dropped in but
# never registered would silently never compile or run. This turns that omission
# into a configure-time FATAL_ERROR.
#
# Scope is deliberately non-recursive `tests/*.cpp`:
#   * tests/compile_fail/*.cpp is the separate MUST-FAIL suite (roadmap 64) and
#     must NOT be added to PJH_JSON_TEST_SOURCES;
#   * header self-containment TUs are generated into the build dir, not the
#     source tree;
#   * header_isolation.cpp belongs to its own executable and is registered via
#     PJH_JSON_HEADER_ISOLATION_SOURCES (passed in REGISTERED).
#
# Standalone check from a script (no project configure, no network):
#   include(".../cmake/check_test_sources.cmake")
#   pjh_json_check_test_sources(DIRECTORY "..." REGISTERED a.cpp b.cpp ...)

function(pjh_json_check_test_sources)
    cmake_parse_arguments(ARG "" "DIRECTORY" "REGISTERED" ${ARGN})

    if(NOT ARG_DIRECTORY)
        message(FATAL_ERROR "pjh_json_check_test_sources: DIRECTORY is required")
    endif()

    # CONFIGURE_DEPENDS makes a newly dropped-in file trigger a reconfigure and
    # re-run of this guard. It is rejected by `file(GLOB)` in script mode, so
    # the standalone `cmake -P` self-check falls back to a plain glob.
    if(CMAKE_SCRIPT_MODE_FILE)
        file(GLOB _pjh_candidates "${ARG_DIRECTORY}/*.cpp")
    else()
        file(GLOB _pjh_candidates CONFIGURE_DEPENDS "${ARG_DIRECTORY}/*.cpp")
    endif()

    set(_pjh_unregistered)
    foreach(_pjh_candidate IN LISTS _pjh_candidates)
        get_filename_component(_pjh_name "${_pjh_candidate}" NAME)
        list(FIND ARG_REGISTERED "${_pjh_name}" _pjh_index)
        if(_pjh_index EQUAL -1)
            list(APPEND _pjh_unregistered "${_pjh_name}")
        endif()
    endforeach()

    if(_pjh_unregistered)
        list(JOIN _pjh_unregistered "\n    " _pjh_unregistered_pretty)
        message(FATAL_ERROR
            "Unregistered test source(s) in ${ARG_DIRECTORY}:\n"
            "    ${_pjh_unregistered_pretty}\n"
            "Every top-level tests/*.cpp must be registered. Add doctest TUs to\n"
            "PJH_JSON_TEST_SOURCES in tests/CMakeLists.txt. A file belonging to an\n"
            "independent test executable must be listed in that target's own source\n"
            "variable (e.g. PJH_JSON_HEADER_ISOLATION_SOURCES).\n"
            "tests/compile_fail/*.cpp is a separate MUST-FAIL suite and is not\n"
            "covered by this non-recursive guard.")
    endif()
endfunction()
