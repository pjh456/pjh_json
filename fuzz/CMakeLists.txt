# libFuzzer differential fuzzers. Included only when PJH_JSON_BUILD_FUZZERS=ON
# (root CMakeLists gates: Clang-only, sanitizers required, library built with
# -fsanitize=fuzzer-no-link coverage). The fuzzer runtime and main are linked
# here, never into the static library or its export surface.
# nlohmann_json: system package first when PJH_JSON_DEP_MODE != FETCH. Reuse the
# benchmark's declaration when both are enabled: the NOT-TARGET guard below
# makes the second provider a no-op (same name + same pin + same FORCE).
if(NOT PJH_JSON_DEP_MODE STREQUAL "FETCH")
    find_package(nlohmann_json QUIET)
endif()
if(NOT TARGET nlohmann_json::nlohmann_json)
    if(PJH_JSON_DEP_MODE STREQUAL "SYSTEM")
        pjh_json_dep_missing(nlohmann_json
            "Debian/Ubuntu: nlohmann-json3-dev; macOS: brew install nlohmann-json; vcpkg: nlohmann-json")
    endif()
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

set(PJH_JSON_FUZZ_FLAGS -fsanitize=fuzzer ${PJH_JSON_SANITIZER_FLAGS})

# Keep the committed corpus read-only: copy the seeds to a build-dir working
# corpus, which is what the fuzzer mutates/writes into.
function(pjh_json_fuzz_corpus target src_dir out_dir)
    file(MAKE_DIRECTORY ${out_dir})
    file(COPY ${src_dir}/ DESTINATION ${out_dir})
endfunction()

function(pjh_json_add_fuzzer name corpus)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE pjh::json nlohmann_json::nlohmann_json)
    target_compile_options(${name} PRIVATE ${PJH_JSON_FUZZ_FLAGS})
    target_link_options(${name} PRIVATE ${PJH_JSON_FUZZ_FLAGS})

    # PGO flags are PRIVATE to pjh_json (root CMakeLists). Re-apply them so the
    # GENERATE profile runtime is linked when fuzzers are built with PGO on.
    if(PJH_JSON_PGO_FLAGS)
        target_compile_options(${name} PRIVATE ${PJH_JSON_PGO_FLAGS})
        target_link_options(${name} PRIVATE ${PJH_JSON_PGO_FLAGS})
    endif()

    # Same warning re-apply as tests/CMakeLists.txt (root list). Keep in lockstep.
    if(PJH_JSON_WARNING_FLAGS)
        target_compile_options(${name} PRIVATE ${PJH_JSON_WARNING_FLAGS})
    endif()

    pjh_json_fuzz_corpus(${name}
        ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${corpus}
        ${CMAKE_CURRENT_BINARY_DIR}/corpus/${corpus})

    # Deterministic, bounded smoke gate: fixed seed + run count + committed
    # seeds (read-only second dir) + build-dir working corpus (output dir).
    add_test(NAME ${name}
        COMMAND ${name}
            -runs=5000 -seed=1 -max_len=4096
            -dict=${CMAKE_CURRENT_SOURCE_DIR}/json.dict
            -artifact_prefix=${CMAKE_CURRENT_BINARY_DIR}/
            ${CMAKE_CURRENT_BINARY_DIR}/corpus/${corpus}
            ${CMAKE_CURRENT_SOURCE_DIR}/corpus/${corpus})
endfunction()

pjh_json_add_fuzzer(fuzz_parse_copy  parse_copy)
pjh_json_add_fuzzer(fuzz_parse_jsonl parse_jsonl)
