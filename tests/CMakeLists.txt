# doctest: system package first when PJH_JSON_DEP_MODE != FETCH.
# Version floor 2.5.0: the suite compares std::pmr::basic_string values, which
# doctest only stringifies from 2.5.0 (templated String ctor + is_std_string);
# older system packages otherwise fail to compile the tests with a cryptic
# error deep in doctest.h.
# NOTE: the installed package provides only the imported target
# doctest::doctest (the bare `doctest` target exists only in the FetchContent
# add_subdirectory path), so both executables link doctest::doctest below.
if(NOT PJH_JSON_DEP_MODE STREQUAL "FETCH")
    find_package(doctest 2.5.0 QUIET)
endif()

if(NOT TARGET doctest::doctest)
    if(PJH_JSON_DEP_MODE STREQUAL "SYSTEM")
        pjh_json_dep_missing(doctest
            "doctest >= 2.5.0 (Debian/Ubuntu: doctest-dev; macOS: brew install doctest; vcpkg: doctest)")
    endif()
    include(FetchContent)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG v2.5.0)

    # doctest is a test-only dependency: suppress its own install rules so
    # `cmake --install` does not leak include/doctest, lib/cmake/doctest and
    # lib/pkgconfig/doctest.pc into the consumer prefix. Same pattern as
    # XSIMD_SKIP_INSTALL (root CMakeLists). Must precede MakeAvailable, which
    # add_subdirectory()s doctest and registers its install() commands.
    set(DOCTEST_NO_INSTALL ON CACHE BOOL "Skip doctest install rules" FORCE)
    set(DOCTEST_WITH_MAIN_IN_STATIC_LIB OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(doctest)
endif()

set(PJH_JSON_TEST_SOURCES
    test_main.cpp
    access_result.cpp
    config_document.cpp
    constexpr_json_test.cpp
    differential_errors.cpp
    differential_validation.cpp
    doc_examples.cpp
    error_kernel.cpp
    json5.cpp
    json_constexpr_isolation.cpp
    json_fwd_isolation.cpp
    json_parser.cpp
    json_patch.cpp
    json_types.cpp
    json_writer.cpp
    literal_test.cpp
    merge_patch.cpp
    public_header_boundary.cpp
    read_file.cpp
    schema.cpp
    simd_baseline.cpp
    std_interop.cpp
    stream_reader.cpp
    umbrella_isolation.cpp
)

# Sources of the independent header-isolation executable. Kept as a named
# variable (not a bare filename in add_executable) so the roadmap-66 guard can
# recognize it: a top-level tests/*.cpp may be registered either here or in
# PJH_JSON_TEST_SOURCES, and nowhere else.
set(PJH_JSON_HEADER_ISOLATION_SOURCES
    header_isolation.cpp)

# roadmap 66: configure-time guard. Every top-level tests/*.cpp must be
# registered; tests/compile_fail/ and generated TUs are out of scope.
include(${PROJECT_SOURCE_DIR}/cmake/check_test_sources.cmake)
pjh_json_check_test_sources(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    REGISTERED ${PJH_JSON_TEST_SOURCES} ${PJH_JSON_HEADER_ISOLATION_SOURCES})

add_executable(pjh_json_tests ${PJH_JSON_TEST_SOURCES})
target_link_libraries(pjh_json_tests PRIVATE pjh_json doctest::doctest)

include(${PROJECT_SOURCE_DIR}/cmake/pjh_json_doctest_discover_tests.cmake)
pjh_json_doctest_discover_tests(pjh_json_tests)

# header_isolation.cpp must NOT be linked into pjh_json_tests: sibling TUs
# (json_writer.cpp, json_types.cpp) instantiate the same Array::of /
# Object::of specialisations and emit weak/COMDAT definitions that would
# satisfy this TU's undefined references, masking the json.hpp-only link
# regression. Its own executable guarantees the linker must resolve `of`
# from pjh_json alone.
add_executable(pjh_json_header_isolation ${PJH_JSON_HEADER_ISOLATION_SOURCES})
target_link_libraries(pjh_json_header_isolation PRIVATE pjh_json doctest::doctest)

pjh_json_doctest_discover_tests(pjh_json_header_isolation)

# ---------------------------------------------------------------------------
# Header self-containment (roadmap 64): every INSTALLED public header must
# compile as the first and only pjh include in a TU. The installed surface is
# include/pjh_json.hpp + include/pjh_json/*.hpp (pjh_json/detail/ is
# build-tree-only and intentionally not checked here). Glob the module headers
# so a newly added public header is checked automatically, then generate one
# include-only TU per header into the build dir. This is an OBJECT library: the
# normal `cmake --build` IS the gate (no link, no ctest entry).
#
# Deliberately NOT in PJH_JSON_TEST_SOURCES: these are not doctest TUs. Do not
# add files under tests/self_containment/ to that list.
# ---------------------------------------------------------------------------
file(GLOB PJH_JSON_PUBLIC_MODULE_HEADERS CONFIGURE_DEPENDS
     "${CMAKE_SOURCE_DIR}/include/pjh_json/*.hpp")

file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/self_containment")

set(PJH_JSON_SELFCONTAINMENT_SOURCES)
set(_pjh_selfcheck_idx 0)
foreach(_pjh_hdr IN LISTS PJH_JSON_PUBLIC_MODULE_HEADERS)
    get_filename_component(_pjh_hdr_name "${_pjh_hdr}" NAME_WE)
    set(_pjh_tu
        "${CMAKE_CURRENT_BINARY_DIR}/self_containment/${_pjh_selfcheck_idx}_${_pjh_hdr_name}.cpp")
    file(GENERATE OUTPUT "${_pjh_tu}"
         CONTENT "#include <pjh_json/${_pjh_hdr_name}.hpp>\n")
    list(APPEND PJH_JSON_SELFCONTAINMENT_SOURCES "${_pjh_tu}")
    math(EXPR _pjh_selfcheck_idx "${_pjh_selfcheck_idx} + 1")
endforeach()

# The umbrella is part of the installed surface too.
set(_pjh_umbrella_tu "${CMAKE_CURRENT_BINARY_DIR}/self_containment/umbrella.cpp")
file(GENERATE OUTPUT "${_pjh_umbrella_tu}" CONTENT "#include <pjh_json.hpp>\n")
list(APPEND PJH_JSON_SELFCONTAINMENT_SOURCES "${_pjh_umbrella_tu}")

add_library(pjh_json_header_selfcheck OBJECT ${PJH_JSON_SELFCONTAINMENT_SOURCES})
target_link_libraries(pjh_json_header_selfcheck PRIVATE pjh_json)
if(PJH_JSON_SANITIZER_FLAGS)
    target_compile_options(pjh_json_header_selfcheck PRIVATE ${PJH_JSON_SANITIZER_FLAGS})
endif()

# ---------------------------------------------------------------------------
# Negative compilation tests (roadmap 64): every tests/compile_fail/<case>.cpp
# MUST fail to compile. Each case is an OBJECT library from EXCLUDE_FROM_ALL
# (never built by a normal `cmake --build`); the ctest test builds just that one
# target and WILL_FAIL inverts the expected failure. Using a real CMake target
# (rather than a hand-spelled compiler command) keeps C++20, the pjh_json PUBLIC
# include set (incl. pjh_result) and GCC/Clang/MSVC driver differences
# handled by CMake.
#
# These are NOT doctest TUs -> never add them to PJH_JSON_TEST_SOURCES.
# ---------------------------------------------------------------------------
function(pjh_json_add_compile_fail name)
    add_library(compile_fail_${name} OBJECT EXCLUDE_FROM_ALL
                "${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/${name}.cpp")
    target_link_libraries(compile_fail_${name} PRIVATE pjh_json)
    add_test(NAME compile_fail.${name}
             COMMAND ${CMAKE_COMMAND} --build "${CMAKE_BINARY_DIR}"
                     --target compile_fail_${name})
    set_tests_properties(compile_fail.${name} PROPERTIES
                         WILL_FAIL TRUE RUN_SERIAL TRUE)
endfunction()

file(GLOB PJH_JSON_COMPILE_FAIL_CASES CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/compile_fail/*.cpp")
foreach(_pjh_bad IN LISTS PJH_JSON_COMPILE_FAIL_CASES)
    get_filename_component(_pjh_bad_name "${_pjh_bad}" NAME_WE)
    pjh_json_add_compile_fail("${_pjh_bad_name}")
endforeach()

# Sanitizer flags are PRIVATE to pjh_json (root CMakeLists, same pattern as
# PJH_JSON_PGO_FLAGS). Re-apply them so the test TUs are instrumented and the
# sanitizer runtime is linked into both executables. Keep in lockstep with root.
if(PJH_JSON_SANITIZER_FLAGS)
    target_compile_options(pjh_json_tests PRIVATE ${PJH_JSON_SANITIZER_FLAGS})
    target_link_options(pjh_json_tests PRIVATE ${PJH_JSON_SANITIZER_FLAGS})
    target_compile_options(pjh_json_header_isolation PRIVATE ${PJH_JSON_SANITIZER_FLAGS})
    target_link_options(pjh_json_header_isolation PRIVATE ${PJH_JSON_SANITIZER_FLAGS})
endif()

# PGO flags are PRIVATE to pjh_json (root CMakeLists, same pattern as the
# sanitizer block above). Re-apply them so both test executables link the
# GENERATE profile runtime. Keep in lockstep with the root list.
if(PJH_JSON_PGO_FLAGS)
    target_compile_options(pjh_json_tests PRIVATE ${PJH_JSON_PGO_FLAGS})
    target_link_options(pjh_json_tests PRIVATE ${PJH_JSON_PGO_FLAGS})
    target_compile_options(pjh_json_header_isolation PRIVATE ${PJH_JSON_PGO_FLAGS})
    target_link_options(pjh_json_header_isolation PRIVATE ${PJH_JSON_PGO_FLAGS})
endif()

# Warning flags are PRIVATE to pjh_json (root CMakeLists). Re-apply so the
# test / header-isolation / self-containment TUs are audited too. Keep in
# lockstep with the root variable.
if(PJH_JSON_WARNING_FLAGS)
    target_compile_options(pjh_json_tests PRIVATE ${PJH_JSON_WARNING_FLAGS})
    target_compile_options(pjh_json_header_isolation PRIVATE ${PJH_JSON_WARNING_FLAGS})
    target_compile_options(pjh_json_header_selfcheck PRIVATE ${PJH_JSON_WARNING_FLAGS})
endif()
