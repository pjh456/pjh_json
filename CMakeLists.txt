cmake_minimum_required(VERSION 3.20)
project(
    pjh_json
    VERSION 0.2.0
    LANGUAGES CXX
)

# -------------------------
# Minimum compiler versions
# -------------------------
# Floor inferred from the C++20 features used by the public headers
# (consteval, bit_cast, FP from_chars/to_chars, designated initializers).
# Fail at configure time with an actionable message instead of surfacing N
# cryptic template errors deep inside the library. See STACK.md.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 11)
        message(FATAL_ERROR
            "pjh_json requires GCC >= 11 (found ${CMAKE_CXX_COMPILER_VERSION}).")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 16)
        message(FATAL_ERROR
            "pjh_json requires Clang >= 16 (found ${CMAKE_CXX_COMPILER_VERSION}).")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    # Apple's version does NOT track LLVM: AppleClang 15.0 (Xcode 15) is
    # LLVM 16, the first with C++20 designated initializers.
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
        message(FATAL_ERROR
            "pjh_json requires AppleClang >= 15 / Xcode 15+ "
            "(found ${CMAKE_CXX_COMPILER_VERSION}).")
    endif()
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS 19.29)
        message(FATAL_ERROR
            "pjh_json requires MSVC >= 19.29 / VS 2019 16.10+ "
            "(found ${CMAKE_CXX_COMPILER_VERSION}).")
    endif()
endif()

# export cmake compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# -------------------------
# version.hpp <-> project(VERSION) sync guard
# -------------------------
# The version is exposed to consumers through include/pjh_json/version.hpp as
# well as the CMake package. Keep the two in lockstep so the in-code constants
# and `find_package(pjh_json X.Y)` can never disagree. See VERSIONING.md.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pjh_json/version.hpp")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/include/pjh_json/version.hpp")
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/include/pjh_json/version.hpp" _pjh_version_hpp)
    if(NOT _pjh_version_hpp MATCHES
       "PJH_JSON_VERSION_STRING \"([0-9]+\\.[0-9]+\\.[0-9]+)\"")
        message(FATAL_ERROR
            "include/pjh_json/version.hpp: no PJH_JSON_VERSION_STRING \"X.Y.Z\" found")
    endif()
    if(NOT CMAKE_MATCH_1 STREQUAL pjh_json_VERSION)
        message(FATAL_ERROR
            "version.hpp (${CMAKE_MATCH_1}) != project VERSION "
            "(${pjh_json_VERSION}); update both in the same commit.")
    endif()
endif()

# -------------------------
# option
# -------------------------
option(PJH_JSON_BUILD_TESTS "Build tests" OFF)
option(PJH_JSON_BUILD_EXAMPLES "Build examples" OFF)
option(PJH_JSON_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(PJH_JSON_BUILD_FUZZERS "Build libFuzzer differential fuzz targets (Clang + sanitizers)" OFF)
set(PJH_JSON_PGO OFF CACHE STRING "PGO mode: OFF, GENERATE, or USE")

# -------------------------
# dependency
# -------------------------
set(XSIMD_SKIP_INSTALL ON CACHE BOOL "Skip xsimd install" FORCE)
if(NOT TARGET xsimd)
    add_subdirectory(thirdparty/xsimd)
endif()
if(NOT TARGET pjh_result)
    add_subdirectory(thirdparty/pjh_result)
endif()

# CXX_STANDARD is a per-target property and does not propagate from pjh_json
# to consumer targets; set the directory-scope default (after xsimd so its
# exported interface keeps cxx_std_11) for tests/examples/benchmarks below.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# -------------------------
# source files
# -------------------------
file(GLOB_RECURSE PJH_JSON_SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
)

# -------------------------
# library
# -------------------------
add_library(pjh_json ${PJH_JSON_SOURCES})

add_library(pjh::json ALIAS pjh_json)

target_include_directories(pjh_json
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/xsimd/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/pjh_result/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Consumer-facing imported name: the build-tree alias `pjh::json` (:75) is not
# exported (CMake: ALIAS targets cannot be installed/exported), so the install
# export must reproduce the same spelling: NAMESPACE pjh:: + EXPORT_NAME json
# makes `find_package(pjh_json)` yield `pjh::json`.
set_target_properties(pjh_json PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    EXPORT_NAME json)
target_compile_options(pjh_json PRIVATE -std=c++20)

# -------------------------
# Warnings (first-party code only)
# -------------------------
# Same house rule as PJH_JSON_PGO / PJH_JSON_SANITIZERS: warning flags are
# PRIVATE to pjh_json and re-applied PRIVATE to every in-repo executable, so
# they never reach INTERFACE_COMPILE_OPTIONS / the install export surface.
# No error-promotion flag here: the CI gate is compile+ctest+sanitizers,
# not warnings.
set(PJH_JSON_WARNING_FLAGS)
if(MSVC)
    set(PJH_JSON_WARNING_FLAGS /W4)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    set(PJH_JSON_WARNING_FLAGS -Wall -Wextra)
endif()
if(PJH_JSON_WARNING_FLAGS)
    target_compile_options(pjh_json PRIVATE ${PJH_JSON_WARNING_FLAGS})
endif()

# -------------------------
# Compiler Optimizations
# -------------------------
# The library never injects optimization or ISA flags. Optimization level is
# the consumer's/packager's choice via CMAKE_BUILD_TYPE / CMAKE_CXX_FLAGS;
# in particular -march=native is deliberately NOT used (non-portable artifacts
# and unsupported by AppleClang on arm64). SIMD width follows xsimd's default
# architecture; runtime ISA dispatch is tracked in the roadmap.
#
# Portability rule if an ISA/arch knob is ever added: keep it PRIVATE and
# re-apply it to every in-repo executable (the PJH_JSON_PGO_FLAGS /
# PJH_JSON_SANITIZER_FLAGS pattern), and spell it per architecture:
#   - x86-64:            -march=native (or -march=<target>)
#   - arm64/AppleClang:  -mcpu=native   (-march=native is rejected on Apple
#                        Silicon and incomplete on Clang)
#   - MSVC:              no native equivalent (no-op)
# Never pass -mcpu and -march together. Runtime ISA dispatch (baseline ISA +
# runtime CPU detection) is the portable alternative; see the roadmap.
#
# PGO flags are PRIVATE to pjh_json: a pre-built static library cannot be
# re-profiled by a consumer, and inherited instrumentation/link flags would be
# meaningless (and, for GENERATE, unsound) on the consumer side. The same list
# is re-applied PRIVATE to every in-repo executable that links the library
# (tests / benchmarks / examples / fuzz) so the GENERATE runtime is linked and
# the profiling workload can run. Keep those re-applies in lockstep.
if(PJH_JSON_PGO STREQUAL "GENERATE")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(PJH_JSON_PGO_FLAGS -fprofile-generate)
    elseif(MSVC)
        set(PJH_JSON_PGO_FLAGS /GENPROFILE)
    endif()
elseif(PJH_JSON_PGO STREQUAL "USE")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set(PJH_JSON_PGO_FLAGS -fprofile-use)
    elseif(MSVC)
        set(PJH_JSON_PGO_FLAGS /USEPROFILE)
    endif()
endif()

if(PJH_JSON_PGO_FLAGS)
    target_compile_options(pjh_json PRIVATE ${PJH_JSON_PGO_FLAGS})
    target_link_options(pjh_json PRIVATE ${PJH_JSON_PGO_FLAGS})
endif()

# -------------------------
# Sanitizers (ASan / UBSan / TSan) — developer/CI instrumentation only
# -------------------------
# Single string option: OFF (default) or a comma-separated GCC/Clang sanitizer
# list, e.g. "address,undefined" (ASan+UBSan, combinable) or "thread" (TSan,
# must stand alone). These are instrumentation flags and follow the same house
# rule as PJH_JSON_PGO: PRIVATE to pjh_json, re-applied PRIVATE to every in-repo
# executable. They are deliberately NOT exported: a sanitized static library
# references the sanitizer runtime and is a test artifact, never a consumer one.
set(PJH_JSON_SANITIZERS OFF CACHE STRING
    "Sanitizers: OFF, or a comma-separated GCC/Clang list (address,undefined / thread)")

set(PJH_JSON_SANITIZER_FLAGS)
if(NOT PJH_JSON_SANITIZERS STREQUAL "OFF" AND NOT PJH_JSON_SANITIZERS STREQUAL "")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        if(PJH_JSON_SANITIZERS MATCHES "thread"
           AND PJH_JSON_SANITIZERS MATCHES "address|undefined")
            message(FATAL_ERROR
                "PJH_JSON_SANITIZERS: TSan (thread) cannot be combined with "
                "ASan/UBSan; use separate build trees.")
        endif()
        set(PJH_JSON_SANITIZER_FLAGS
            "-fsanitize=${PJH_JSON_SANITIZERS}"
            -fno-omit-frame-pointer
            -fno-sanitize-recover=all)
    else()
        message(FATAL_ERROR
            "PJH_JSON_SANITIZERS is supported only with GCC/Clang; "
            "${CMAKE_CXX_COMPILER_ID} is not supported.")
    endif()
endif()

if(PJH_JSON_SANITIZER_FLAGS)
    target_compile_options(pjh_json PRIVATE ${PJH_JSON_SANITIZER_FLAGS})
    target_link_options(pjh_json PRIVATE ${PJH_JSON_SANITIZER_FLAGS})
endif()

if(MSVC)
    add_compile_options(
        /utf-8
        /MP
        /Zc:char8_t-
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # -static is incompatible with the ASan/TSan runtimes (fully-static
    # sanitizer executables are unsupported); keep the default fully-static
    # link only in uninstrumented builds.
    if(NOT PJH_JSON_SANITIZER_FLAGS)
        add_link_options(
            -static-libgcc
            -static-libstdc++
            -static)
    endif()
endif()

# -------------------------
# link dependencies
# -------------------------
target_link_libraries(pjh_json
    PUBLIC
    xsimd
    pjh_result
)

# -------------------------
# install / export（仅独立工程时生成；作为子项目被 add_subdirectory 时跳过，
# 否则导出集缺少外部提供的 pjh_result 目标会导致 configure 失败）
# -------------------------
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(pjh_json_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/pjh_json)

    install(TARGETS pjh_json xsimd EXPORT pjh_jsonTargets)   # pjh_result 不入集（文件级守卫全有全无）
    install(EXPORT pjh_jsonTargets
        NAMESPACE pjh::
        FILE pjh_jsonTargets.cmake
        DESTINATION ${pjh_json_INSTALL_CMAKEDIR})
    # Public headers only: pjh_json/detail/ holds internal headers reachable in
    # the build tree (src/ and tests/ include them) but NOT installed. Keep new
    # internal headers under include/pjh_json/detail/ - the exclude below is the
    # single enforcement point.
    install(DIRECTORY include/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "detail" EXCLUDE)
    install(DIRECTORY thirdparty/xsimd/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(DIRECTORY thirdparty/pjh_result/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

    configure_package_config_file(cmake/pjh_jsonConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/pjh_jsonConfig.cmake
        INSTALL_DESTINATION ${pjh_json_INSTALL_CMAKEDIR})
    # Pre-1.0 the compatibility boundary is the MINOR version (SemVer §4):
    # a 0.y consumer must not be silently satisfied by a 0.(y+1) package that
    # may have removed API. From 1.0.0 the usual major boundary applies.
    # See VERSIONING.md.
    if(pjh_json_VERSION_MAJOR EQUAL 0)
        set(PJH_JSON_PACKAGE_COMPATIBILITY SameMinorVersion)
    else()
        set(PJH_JSON_PACKAGE_COMPATIBILITY SameMajorVersion)
    endif()
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/pjh_jsonConfigVersion.cmake
        VERSION ${pjh_json_VERSION}
        COMPATIBILITY ${PJH_JSON_PACKAGE_COMPATIBILITY})
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/pjh_jsonConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/pjh_jsonConfigVersion.cmake
        DESTINATION ${pjh_json_INSTALL_CMAKEDIR})
endif()

# -------------------------
# tests/examples
# -------------------------
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    enable_testing()

    # -------------------------
    # libFuzzer differential fuzzers (opt-in; Clang + sanitizers only)
    # -------------------------
    # The library objects need the fuzzer coverage pass (fuzzer-no-link) for
    # libFuzzer's feedback to see the parser internals; the fuzzer runtime and
    # main are linked only by the fuzz executables (fuzz/CMakeLists.txt).
    if(PJH_JSON_BUILD_FUZZERS)
        if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            message(FATAL_ERROR
                "PJH_JSON_BUILD_FUZZERS requires Clang (libFuzzer); "
                "${CMAKE_CXX_COMPILER_ID} is not supported. Use a Clang build tree.")
        endif()
        if(NOT PJH_JSON_SANITIZER_FLAGS)
            message(FATAL_ERROR
                "PJH_JSON_BUILD_FUZZERS requires sanitizers: configure with "
                "-DPJH_JSON_SANITIZERS=address,undefined.")
        endif()
        if(PJH_JSON_SANITIZERS MATCHES "thread")
            message(FATAL_ERROR
                "PJH_JSON_BUILD_FUZZERS is incompatible with TSan (thread); "
                "use -DPJH_JSON_SANITIZERS=address,undefined.")
        endif()
        target_compile_options(pjh_json PRIVATE -fsanitize=fuzzer-no-link)
        add_subdirectory(fuzz)
    endif()

    if(PJH_JSON_BUILD_TESTS)
        add_subdirectory(tests)
    endif()

    if(PJH_JSON_BUILD_EXAMPLES)
        add_subdirectory(examples)
    endif()

    if(PJH_JSON_BUILD_BENCHMARKS)
        add_subdirectory(benchmarks)
    endif()
endif()