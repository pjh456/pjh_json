include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.5.0)

set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

set(PJH_JSON_TEST_SOURCES
    test_main.cpp
    access_result.cpp
    config_document.cpp
    constexpr_json_test.cpp
    json_parser.cpp
    json_types.cpp
    json_writer.cpp
    literal_test.cpp
    read_file.cpp
    std_interop.cpp
)

add_executable(pjh_json_tests ${PJH_JSON_TEST_SOURCES})
target_link_libraries(pjh_json_tests PRIVATE pjh_json doctest)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(pjh_json_tests)

# header_isolation.cpp must NOT be linked into pjh_json_tests: sibling TUs
# (json_writer.cpp, json_types.cpp) instantiate the same Array::of /
# Object::of specialisations and emit weak/COMDAT definitions that would
# satisfy this TU's undefined references, masking the json.hpp-only link
# regression. Its own executable guarantees the linker must resolve `of`
# from pjh_json alone.
add_executable(pjh_json_header_isolation header_isolation.cpp)
target_link_libraries(pjh_json_header_isolation PRIVATE pjh_json doctest)

doctest_discover_tests(pjh_json_header_isolation)
