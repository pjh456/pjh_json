set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

include(FetchContent)

# 1. 引入 Google Benchmark
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(benchmark)

# 2. 引入 nlohmann_json 用于对比
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# 3. 引入 rapidjson 用于对比
FetchContent_Declare(
    rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG v1.1.0
    PATCH_COMMAND ${CMAKE_COMMAND}
        -Dsrc=${CMAKE_BINARY_DIR}/_deps/rapidjson-src/include/rapidjson/document.h
        -P ${CMAKE_CURRENT_SOURCE_DIR}/rapidjson_patch.cmake
)
set(RAPIDJSON_BUILD_DOC OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(RAPIDJSON_BUILD_THIRDPARTY_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rapidjson)

# 创建可执行程序
add_executable(pjh_json_benchmark benchmark_test.cpp)

# 链接并包含头文件
target_link_libraries(pjh_json_benchmark
    PRIVATE
    pjh::json
    benchmark::benchmark
    benchmark::benchmark_main
    nlohmann_json::nlohmann_json
)

# rapidjson 没有现代的 cmake target，通过源码目录引入
target_include_directories(pjh_json_benchmark PRIVATE ${rapidjson_SOURCE_DIR}/include)

# Same sanitizer re-apply as tests/CMakeLists.txt (root list). Keep in lockstep.
if(PJH_JSON_SANITIZER_FLAGS)
    target_compile_options(pjh_json_benchmark PRIVATE ${PJH_JSON_SANITIZER_FLAGS})
    target_link_options(pjh_json_benchmark PRIVATE ${PJH_JSON_SANITIZER_FLAGS})
endif()

# Benchmark 数据目录（roadmap 67）：生成物写进构建目录，不再污染仓库根。
# 默认每棵构建树一份；想让多树共享 ~1.8 GiB 数据，可把 PJH_JSON_BENCH_DATA_DIR
# 指到公共缓存。编译期把绝对路径（正斜杠）传给可执行程序。
set(PJH_JSON_BENCH_DATA_DIR "${CMAKE_CURRENT_BINARY_DIR}/data" CACHE PATH
    "生成的 benchmark *.json 数据目录（默认在构建树内）")

if(IS_ABSOLUTE "${PJH_JSON_BENCH_DATA_DIR}")
    set(_pjh_json_bench_data_dir "${PJH_JSON_BENCH_DATA_DIR}")
else()
    set(_pjh_json_bench_data_dir "${CMAKE_CURRENT_BINARY_DIR}/${PJH_JSON_BENCH_DATA_DIR}")
endif()
file(TO_CMAKE_PATH "${_pjh_json_bench_data_dir}" _pjh_json_bench_data_dir)
file(MAKE_DIRECTORY "${_pjh_json_bench_data_dir}")

# 生成器确定性种子（std::mt19937）：固定默认值 ⇒ 同一工具链下每次生成的数据
# 逐字节一致（性能门需要稳定输入）。改种子即故意重排，需先清旧数据。
set(PJH_JSON_BENCH_SEED "0xC0FFEE" CACHE STRING
    "benchmark JSON 生成器的确定性种子（32 位无符号）")

target_compile_definitions(pjh_json_benchmark
    PRIVATE
    PJH_JSON_BENCH_DATA_DIR="${_pjh_json_bench_data_dir}"
    PJH_JSON_BENCH_SEED=${PJH_JSON_BENCH_SEED}
)

# 清理（roadmap 67 的「清理」半边）：
#   * ADDITIONAL_CLEAN_FILES 让 `cmake --build <build> --target clean`
#     连同构建产物一起删掉数据目录；
#   * 显式目标额外删掉 task 67 之前留在仓库根的 8 个旧文件（gitignored，
#     但仍是工作树垃圾）。只在手动调用时执行，绝不隐式跑。
set(_pjh_json_bench_legacy_root_files)
foreach(_pjh_name IN ITEMS
        1mb.json 10mb.json 30mb.json 50mb.json 100mb.json 200mb.json 500mb.json 1gb.json)
    list(APPEND _pjh_json_bench_legacy_root_files "${CMAKE_SOURCE_DIR}/${_pjh_name}")
endforeach()

set_property(TARGET pjh_json_benchmark APPEND PROPERTY
    ADDITIONAL_CLEAN_FILES "${_pjh_json_bench_data_dir}")

add_custom_target(pjh_json_benchmark_clean_data
    COMMENT "删除生成的 benchmark 数据（${_pjh_json_bench_data_dir}）与仓库根旧副本"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${_pjh_json_bench_data_dir}"
    COMMAND ${CMAKE_COMMAND} -E rm -f ${_pjh_json_bench_legacy_root_files}
    VERBATIM
)
