include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.0)

set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

set(PJH_JSON_TEST_SOURCES
    test_main.cpp
    config_document.cpp
    constexpr_json_test.cpp
    json_parser.cpp
    json_types.cpp
    json_writer.cpp
    literal_test.cpp
    read_file.cpp
)

add_executable(pjh_json_tests ${PJH_JSON_TEST_SOURCES})
target_link_libraries(pjh_json_tests PRIVATE pjh_json doctest)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(pjh_json_tests)
