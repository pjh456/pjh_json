cmake_minimum_required(VERSION 3.15)
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
option(PJH_JSON_ENABLE_OPTIMIZATIONS "Enable compiler optimizations (-O3, -march=native, etc.)" ON)
set(PJH_JSON_PGO OFF CACHE STRING "PGO mode: OFF, GENERATE, or USE")

# -------------------------
# dependency
# -------------------------
set(XSIMD_SKIP_INSTALL ON CACHE BOOL "Skip xsimd install" FORCE)
if(NOT TARGET xsimd)
    add_subdirectory(thirdparty/xsimd)
endif()

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
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

set_target_properties(pjh_json PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(pjh_json PRIVATE -std=c++20)

# -------------------------
# Compiler Optimizations
# -------------------------
if(PJH_JSON_ENABLE_OPTIMIZATIONS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pjh_json PUBLIC -O3 -march=native -ffast-math)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(pjh_json PUBLIC /O2 /arch:AVX2 /fp:fast)
    endif()

    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_supported LANGUAGES CXX)
    if(_ipo_supported)
        set_property(TARGET pjh_json PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        # INTERPROCEDURAL_OPTIMIZATION 只作用于本目标自身的编译/链接，
        # 不会把 LTO 链接选项传播给消费者。若不加 INTERFACE 链接选项，
        # 消费者的链接命令将缺少 -flto，GNU ld 遇到静态库中的 LLVM
        # bitcode 目标会报 "file format not recognized"。
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_link_options(pjh_json INTERFACE -flto=thin)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            target_link_options(pjh_json INTERFACE -flto)
        elseif(MSVC)
            target_link_options(pjh_json INTERFACE /LTCG)
        endif()
    endif()
endif()

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
)

# -------------------------
# install / export
# -------------------------
set(pjh_json_INSTALL_CMAKEDIR ${CMAKE_INSTALL_LIBDIR}/cmake/pjh_json)

install(TARGETS pjh_json xsimd EXPORT pjh_jsonTargets)
install(EXPORT pjh_jsonTargets
    FILE pjh_jsonTargets.cmake
    DESTINATION ${pjh_json_INSTALL_CMAKEDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(DIRECTORY thirdparty/xsimd/include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

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