cmake_minimum_required(VERSION 3.15)
project(
    pjh_json
    VERSION 0.01
    LANGUAGES CXX
)

# export cmake compile commands
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# -------------------------
# option
# -------------------------
option(PJH_JSON_BUILD_TESTS "Build tests" OFF)
option(PJH_JSON_BUILD_EXAMPLES "Build examples" OFF)
option(PJH_JSON_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(PJH_JSON_ENABLE_OPTIMIZATIONS "Enable compiler optimizations (-O3, -march=native, etc.)" ON)

# -------------------------
# dependency
# -------------------------
set(XSIMD_SKIP_INSTALL ON CACHE BOOL "Skip xsimd install" FORCE)
add_subdirectory(thirdparty/xsimd)

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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include/xsimd/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_features(pjh_json PUBLIC cxx_std_20)

# -------------------------
# Compiler Optimizations
# -------------------------
if(PJH_JSON_ENABLE_OPTIMIZATIONS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(pjh_json PUBLIC -O3 -march=native -ffast-math)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
        target_compile_options(pjh_json PUBLIC /O2 /arch:AVX2 /fp:fast)
    endif()
endif()

if(MSVC)
    add_compile_options(
        /utf-8
        /MP
        /Zc:char8_t-
    )
else()
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
# tests/examples
# -------------------------
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    enable_testing()

    if(PJH_JSON_BUILD_TESTS)
        add_subdirectory(tests)
    endif()

    # if(PJH_JSON_BUILD_EXAMPLES)
    # add_subdirectory(examples)
    # endif()
    if(PJH_JSON_BUILD_BENCHMARKS)
        add_subdirectory(benchmarks)
    endif()
endif()