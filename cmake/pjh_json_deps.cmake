# cmake/pjh_json_deps.cmake
#
# Dependency acquisition policy shared by tests/, benchmarks/ and fuzz/
# (roadmap 68). The PJH_JSON_DEP_MODE option itself is declared in the root
# CMakeLists, which includes this file right after the option block.
#
# The CMake floor is 3.20, so FetchContent_Declare(... FIND_PACKAGE_ARGS ...)
# (CMake 3.24) is unavailable. Each caller instead uses the pre-3.24 idiom:
#   if(NOT PJH_JSON_DEP_MODE STREQUAL "FETCH") find_package(<pkg> QUIET) endif()
#   if(NOT TARGET <imported target>) ... fetch or fail ...
# This is the same NOT-TARGET guard already used for xsimd/pjh_result
# (root CMakeLists) and nlohmann in fuzz/CMakeLists.txt.

function(pjh_json_dep_missing dep hint)
    message(FATAL_ERROR
        "PJH_JSON_DEP_MODE=SYSTEM: required dependency '${dep}' was not found "
        "by find_package(), and FetchContent is disabled in SYSTEM mode "
        "(offline).\n"
        "Install it (${hint}), or configure with "
        "-DPJH_JSON_DEP_MODE=FETCH (or AUTO) to allow a configure-time fetch.")
endfunction()
