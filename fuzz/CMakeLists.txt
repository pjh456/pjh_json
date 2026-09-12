# libFuzzer differential fuzzers. Included only when PJH_JSON_BUILD_FUZZERS=ON
# (root CMakeLists gates: Clang-only, sanitizers required, library built with
# -fsanitize=fuzzer-no-link coverage). The fuzzer runtime and main are linked
# here, never into the static library or its export surface.
include(FetchContent)

# Reuse the benchmark's exact declaration so enabling both in one tree does not
# fetch nlohmann twice (same name + same pin + same FORCE => no-op second time).
if(NOT TARGET nlohmann_json::nlohmann_json)
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
