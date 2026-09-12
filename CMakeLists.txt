cmake_minimum_required(VERSION 3.20)
project(
    pjh_json
    VERSION 0.01
    LANGUAGES CXX
)

# export cmake compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# -------------------------
# option
# -------------------------
option(PJH_JSON_BUILD_TESTS "Build tests" OFF)
option(PJH_JSON_BUILD_EXAMPLES "Build examples" OFF)
option(PJH_JSON_BUILD_BENCHMARKS "Build benchmarks" OFF)
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

set_target_properties(pjh_json PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(pjh_json PRIVATE -std=c++20)

# -------------------------
# Compiler Optimizations
# -------------------------
# The library never injects optimization or ISA flags. Optimization level is
# the consumer's/packager's choice via CMAKE_BUILD_TYPE / CMAKE_CXX_FLAGS;
# in particular -march=native is deliberately NOT used (non-portable artifacts
# and unsupported by AppleClang on arm64). SIMD width follows xsimd's default
# architecture; runtime ISA dispatch is tracked in the roadmap.
if(PJH_JSON_PGO STREQUAL "GENERATE")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pjh_json PUBLIC -fprofile-generate)
        target_link_options(pjh_json PUBLIC -fprofile-generate)
    elseif(MSVC)
        target_compile_options(pjh_json PUBLIC /GENPROFILE)
        target_link_options(pjh_json PUBLIC /GENPROFILE)
    endif()
elseif(PJH_JSON_PGO STREQUAL "USE")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pjh_json PUBLIC -fprofile-use)
        target_link_options(pjh_json PUBLIC -fprofile-use)
    elseif(MSVC)
        target_compile_options(pjh_json PUBLIC /USEPROFILE)
        target_link_options(pjh_json PUBLIC /USEPROFILE)
    endif()
endif()

if(MSVC)
    add_compile_options(
        /utf-8
        /MP
        /Zc:char8_t-
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    add_link_options(
        -static-libgcc
        -static-libstdc++
        -static)
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
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/pjh_jsonConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion)
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