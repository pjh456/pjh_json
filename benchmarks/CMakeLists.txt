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
    GIT_TAG master
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