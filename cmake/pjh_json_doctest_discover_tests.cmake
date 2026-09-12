# cmake/pjh_json_doctest_discover_tests.cmake
#
# PRE_TEST doctest discovery (roadmap 66).
#
# doctest's bundled doctest_discover_tests (pinned v2.5.0, and upstream master)
# only supports POST_BUILD discovery: it executes the freshly built test binary
# during `cmake --build` to enumerate cases. Two consequences:
#   * cross-compiling breaks unless CROSSCOMPILING_EMULATOR is set, because the
#     build host cannot run the target binary;
#   * the binary runs in the *build* step environment, while CI exports
#     ASAN_OPTIONS/UBSAN_OPTIONS/TSAN_OPTIONS only on the *ctest* step.
# This mirrors CMake's GoogleTest DISCOVERY_MODE=PRE_TEST: the first time ctest
# processes the test list it enumerates cases and includes the generated
# add_test() calls. The parser stays doctest's own doctestAddTests.cmake (run as
# a script); only *when* it runs changes.
#
# Requires FetchContent_MakeAvailable(doctest) to have run (doctest_SOURCE_DIR).

function(pjh_json_doctest_discover_tests TARGET)
    cmake_parse_arguments(
        ""
        ""
        "TEST_PREFIX;TEST_SUFFIX;WORKING_DIRECTORY;TEST_LIST;JUNIT_OUTPUT_DIR"
        "TEST_SPEC;EXTRA_ARGS;PROPERTIES;ADD_LABELS"
        ${ARGN}
    )

    if(NOT _WORKING_DIRECTORY)
        set(_WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    if(NOT _TEST_LIST)
        set(_TEST_LIST ${TARGET}_TESTS)
    endif()

    # Unique per (target, spec, args), same scheme as doctest's own helper.
    string(SHA1 _args_hash "${_TEST_SPEC} ${_EXTRA_ARGS}")
    string(SUBSTRING ${_args_hash} 0 7 _args_hash)

    set(_base "${CMAKE_CURRENT_BINARY_DIR}/${TARGET}_pretest-${_args_hash}")
    set(_include_file "${_base}_include.cmake")
    set(_tests_file "${_base}_tests.cmake")

    get_property(_crosscompiling_emulator TARGET ${TARGET}
        PROPERTY CROSSCOMPILING_EMULATOR)
    get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

    if(_is_multi_config)
        set(_tests_file "${_base}_tests-$<CONFIG>.cmake")
    endif()

    # The pinned doctest's parser, invoked in script mode at ctest time.
    # Resolve it for both acquisition paths:
    #   * FetchContent  -> ${doctest_SOURCE_DIR}/scripts/cmake/doctestAddTests.cmake
    #   * installed pkg -> ${doctest_DIR}/doctestAddTests.cmake (installed beside
    #     doctestConfig.cmake; doctest_DIR is set by find_package in tests/).
    if(NOT _pjh_doctest_discover_script)
        if(DEFINED doctest_SOURCE_DIR
           AND EXISTS "${doctest_SOURCE_DIR}/scripts/cmake/doctestAddTests.cmake")
            set(_pjh_doctest_discover_script
                "${doctest_SOURCE_DIR}/scripts/cmake/doctestAddTests.cmake")
        elseif(DEFINED doctest_DIR
               AND EXISTS "${doctest_DIR}/doctestAddTests.cmake")
            set(_pjh_doctest_discover_script
                "${doctest_DIR}/doctestAddTests.cmake")
        else()
            # Last resort: upstream's own helper sets _DOCTEST_DISCOVER_TESTS_SCRIPT.
            include(doctest)
            set(_pjh_doctest_discover_script "${_DOCTEST_DISCOVER_TESTS_SCRIPT}")
        endif()
    endif()
    set(_discover_script "${_pjh_doctest_discover_script}")

    string(CONCAT _include_content
        "if(EXISTS \"$<TARGET_FILE:${TARGET}>\")" "\n"
        "  if(NOT EXISTS \"${_tests_file}\" OR" "\n"
        "     NOT \"${_tests_file}\" IS_NEWER_THAN \"$<TARGET_FILE:${TARGET}>\" OR" "\n"
        "     NOT \"${_tests_file}\" IS_NEWER_THAN \"\${CMAKE_CURRENT_LIST_FILE}\")" "\n"
        "    execute_process(" "\n"
        "      COMMAND \"${CMAKE_COMMAND}\"" "\n"
        "        -D \"TEST_TARGET=${TARGET}\"" "\n"
        "        -D \"TEST_EXECUTABLE=$<TARGET_FILE:${TARGET}>\"" "\n"
        "        -D \"TEST_EXECUTOR=${_crosscompiling_emulator}\"" "\n"
        "        -D \"TEST_WORKING_DIR=${_WORKING_DIRECTORY}\"" "\n"
        "        -D \"TEST_SPEC=${_TEST_SPEC}\"" "\n"
        "        -D \"TEST_EXTRA_ARGS=${_EXTRA_ARGS}\"" "\n"
        "        -D \"TEST_PROPERTIES=${_PROPERTIES}\"" "\n"
        "        -D \"TEST_ADD_LABELS=${_ADD_LABELS}\"" "\n"
        "        -D \"TEST_PREFIX=${_TEST_PREFIX}\"" "\n"
        "        -D \"TEST_SUFFIX=${_TEST_SUFFIX}\"" "\n"
        "        -D \"TEST_LIST=${_TEST_LIST}\"" "\n"
        "        -D \"TEST_JUNIT_OUTPUT_DIR=${_JUNIT_OUTPUT_DIR}\"" "\n"
        "        -D \"CTEST_FILE=${_tests_file}\"" "\n"
        "        -P \"${_discover_script}\"" "\n"
        "      RESULT_VARIABLE _pjh_discover_result)" "\n"
        "    if(NOT _pjh_discover_result EQUAL 0)" "\n"
        "      message(FATAL_ERROR \"doctest PRE_TEST discovery failed for ${TARGET}\")" "\n"
        "    endif()" "\n"
        "  endif()" "\n"
        "  include(\"${_tests_file}\")" "\n"
        "else()" "\n"
        "  add_test(${TARGET}_NOT_BUILT ${TARGET}_NOT_BUILT)" "\n"
        "endif()" "\n"
    )

    if(_is_multi_config)
        foreach(_config ${CMAKE_CONFIGURATION_TYPES})
            file(GENERATE
                OUTPUT "${_base}_include-${_config}.cmake"
                CONTENT "${_include_content}"
                CONDITION $<CONFIG:${_config}>)
        endforeach()
        file(WRITE "${_include_file}"
            "include(\"${_base}_include-\${CTEST_CONFIGURATION_TYPE}.cmake\")")
    else()
        file(GENERATE OUTPUT "${_include_file}" CONTENT "${_include_content}")
    endif()

    set_property(DIRECTORY APPEND PROPERTY TEST_INCLUDE_FILES "${_include_file}")
endfunction()
