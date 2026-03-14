cmake_minimum_required(VERSION 3.15)
project(
    pjh_json
    VERSION 0.01
    LANGUAGES CXX
)

# -------------------------
# option
# -------------------------
option(PJH_JSON_BUILD_TESTS "Build tests" OFF)
option(PJH_JSON_BUILD_EXAMPLES "Build examples" OFF)

# -------------------------
# dependency
# -------------------------
add_subdirectory(include/xsimd)

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
    $<INSTALL_INTERFACE:include>
)

target_compile_features(pjh_json PUBLIC cxx_std_20)

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
endif()