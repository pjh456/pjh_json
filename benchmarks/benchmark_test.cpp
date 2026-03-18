#include <fstream>
#include <sstream>
#include <string>
#include <random>
#include <iostream>
#include <filesystem>
#include <vector>

#include <benchmark/benchmark.h>

// 被测试库 pjh_json
#include "pjh_json/json.hpp"

// 对比库 nlohmann
#include <nlohmann/json.hpp>

// 对比库 rapidjson
#include <rapidjson/document.h>

static std::mt19937 rng(std::random_device{}());

std::string random_string(size_t length)
{
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; i++)
    {
        result.push_back(charset[dist(rng)]);
    }
    return result;
}

// 使用 nlohmann 协助生成测试用例（因为目前的 pjh::json 尚未实现 serialize）
nlohmann::json random_json_gen(int depth = 0, int max_depth = 5)
{
    std::uniform_int_distribution<int> type_dist(0, 5);
    int t = (depth >= max_depth) ? type_dist(rng) % 3 : type_dist(rng);

    switch (t)
    {
    case 0:
        return nullptr;
    case 1:
        return (bool)(rng() % 2);
    case 2:
        return (float)((rng() % 10000) / 10.0);
    case 3:
        return random_string(5 + rng() % 10);
    case 4:
    { // array
        auto arr = nlohmann::json::array();
        int n = 1 + (rng() % 5);
        for (int i = 0; i < n; i++)
        {
            arr.push_back(random_json_gen(depth + 1, max_depth));
        }
        return arr;
    }
    case 5:
    { // object
        auto obj = nlohmann::json::object();
        int n = 1 + (rng() % 5);
        for (int i = 0; i < n; i++)
        {
            obj[random_string(3 + rng() % 5)] = random_json_gen(depth + 1, max_depth);
        }
        return obj;
    }
    }
    return nullptr;
}

void generate_json_file(const std::string &path, size_t target_size, int max_depth = 5)
{
    std::ofstream ofs(path);
    ofs << "[";

    size_t size = 1;
    bool first = true;

    while (size < target_size)
    {
        auto obj = random_json_gen(0, max_depth).dump();

        if (!first)
        {
            ofs << ",";
            size++;
        }

        ofs << obj;
        size += obj.size();

        first = false;
    }

    ofs << "]";
}

inline std::string read_file(const std::string &path_str)
{
    const std::filesystem::path path(path_str);
    if (!std::filesystem::exists(path))
    {
        std::cerr << "Error: File not found at " << path << std::endl;
        return "";
    }
    std::ifstream ifs(path);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

// ---------------------------------------------------------
// Benchmarks
// ---------------------------------------------------------

// nlohmann_json
static void BM_Nlohmann_Json_Parse(benchmark::State &state, const std::string &content)
{
    for (auto _ : state)
    {
        auto j = nlohmann::json::parse(content);
        benchmark::DoNotOptimize(j);
    }
}

// RapidJSON
static void BM_Rapid_Json_Parse(benchmark::State &state, const std::string &content)
{
    for (auto _ : state)
    {
        rapidjson::Document d;
        d.Parse(content.c_str());
        benchmark::DoNotOptimize(d);
    }
}

// pjh::json
static void BM_PJH_Json_Parse(benchmark::State &state, const std::string &content)
{
    // 预分配一块足够大的连续内存作为后备 Buffer
    // 经验值：DOM 树在内存中的大小通常是 JSON 文本长度的 3 ~ 4 倍，给 5 倍绝对安全
    std::vector<std::byte> memory_buffer(content.size() * 5);

    for (auto _ : state)
    {
        // 每次迭代重新初始化 monotonic_buffer_resource，这会瞬间重置指针，O(1) 释放全部内存
        std::pmr::monotonic_buffer_resource
            pool(
                memory_buffer.data(),
                memory_buffer.size(),
                std::pmr::new_delete_resource());

        // 传入 pool，让 Parser、Array、Object 全部在这个连续 buffer 上极速分配
        pjh::json::Parser parser(content, &pool);
        pjh::json::Json root = std::move(parser.parse());

        benchmark::DoNotOptimize(root);
    }
}

// 动态注册 Benchmarks
void RegisterBenchmarks()
{
    // 定义要测试的数据档位
    std::vector<std::pair<std::string, size_t>> sizes = {
        {"1mb.json", 1 * 1024 * 1024},
        {"10mb.json", 10 * 1024 * 1024},
        {"30mb.json", 30 * 1024 * 1024},
        {"50mb.json", 50 * 1024 * 1024},
        {"100mb.json", 100 * 1024 * 1024},
    };

    for (auto &[fname, target_size] : sizes)
    {
        std::string path = fname;

        // 自动生成测试文件，保证开箱即用
        if (!std::filesystem::exists(path))
        {
            std::cout << "Generating test file " << path << " (" << target_size << " bytes)...\n";
            generate_json_file(path, target_size);
        }

        std::string json_data = read_file(path);

        std::string bm_pjh = "PJH/" + fname;
        std::string bm_nlohmann = "Nlohmann/" + fname;
        std::string bm_rapid = "RapidJSON/" + fname;

        benchmark::RegisterBenchmark(bm_pjh.c_str(), BM_PJH_Json_Parse, json_data);
        benchmark::RegisterBenchmark(bm_nlohmann.c_str(), BM_Nlohmann_Json_Parse, json_data);
        benchmark::RegisterBenchmark(bm_rapid.c_str(), BM_Rapid_Json_Parse, json_data);
    }
}

// Entry Point
int main(int argc, char **argv)
{
    RegisterBenchmarks();
    ::benchmark::Initialize(&argc, argv);
    ::benchmark::RunSpecifiedBenchmarks();
}