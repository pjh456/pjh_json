#include <cstdlib>
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
#include "pjh_json/document.hpp"

// 对比库 nlohmann
#include <nlohmann/json.hpp>

// 对比库 rapidjson
#include <rapidjson/document.h>

// 确定性种子（roadmap 67）：固定默认值使生成的数据可复现；CMake 通过
// -DPJH_JSON_BENCH_SEED=<32 位无符号> 覆盖。非 CMake 构建走下面的回退值。
#ifndef PJH_JSON_BENCH_SEED
#  define PJH_JSON_BENCH_SEED 0xC0FFEEu
#endif
static std::mt19937 rng(
    static_cast<std::mt19937::result_type>(PJH_JSON_BENCH_SEED));

std::string random_string(size_t length)
{
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";
    // 显式取模代替标准库均匀分布：其引擎→取值映射是实现定义的，
    // 只有取模才能让不同标准库生成逐字节相同的数据。
    constexpr size_t charset_size = sizeof(charset) - 1;  // 去掉结尾 NUL
    std::string result;
    result.reserve(length);
    for (size_t i = 0; i < length; i++)
    {
        result.push_back(charset[rng() % charset_size]);
    }
    return result;
}

// 使用 nlohmann 协助生成测试用例（因为目前的 pjh::json 尚未实现 serialize）
nlohmann::json random_json_gen(int depth = 0, int max_depth = 5)
{
    // 同 random_string()：去掉实现定义的分布，保证可复现。
    int t = (depth >= max_depth) ? static_cast<int>(rng() % 3)
                                 : static_cast<int>(rng() % 6);

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
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs)
    {
        std::cerr << "Error: cannot open " << path << " for writing\n";
        std::exit(1);
    }

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

    ofs.flush();
    if (!ofs)
    {
        std::cerr << "Error: failed writing " << path << "\n";
        std::exit(1);
    }
}

inline std::string read_file(const std::string &path_str)
{
    const std::filesystem::path path(path_str);
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
    {
        std::cerr << "Error: cannot read benchmark data file " << path << "\n";
        std::exit(1);
    }
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
    for (auto _ : state)
    {
        auto doc = pjh::json::parse_copy(content, pjh::json::Storage::Arena);
        benchmark::DoNotOptimize(doc);
    }
}

// 动态注册 Benchmarks
void RegisterBenchmarks()
{
    std::filesystem::path base_dir =
#ifdef PJH_JSON_BENCH_DATA_DIR
        PJH_JSON_BENCH_DATA_DIR;
#else
        std::filesystem::current_path();
#endif

    // 数据目录正常由 CMake 在配置期创建；这里防御性重建，保证直接运行二进制、
    // 或 `clean` 删掉目录后仍可用。
    {
        std::error_code ec;
        std::filesystem::create_directories(base_dir, ec);
        if (ec)
        {
            std::cerr << "Error: cannot create benchmark data dir " << base_dir
                      << ": " << ec.message() << "\n";
            std::exit(1);
        }
    }

    // 定义要测试的数据档位
    std::vector<std::pair<std::string, size_t>> sizes = {
        {"1mb.json", 1ULL * 1024 * 1024},
        {"10mb.json", 10ULL * 1024 * 1024},
        {"30mb.json", 30ULL * 1024 * 1024},
        {"50mb.json", 50ULL * 1024 * 1024},
        {"100mb.json", 100ULL * 1024 * 1024},
        {"200mb.json", 200ULL * 1024 * 1024},
        {"500mb.json", 500ULL * 1024 * 1024},
        {"1gb.json", 1024ULL * 1024 * 1024},
    };

    for (auto &[fname, target_size] : sizes)
    {
        std::filesystem::path path = base_dir / fname;

        // 自动生成测试文件，保证开箱即用
        if (!std::filesystem::exists(path) ||
            std::filesystem::file_size(path) < target_size)
        {
            std::cout << "Generating test file " << path.string() << " (" << target_size << " bytes)...\n";
            generate_json_file(path.string(), target_size);
        }

        std::string json_data = read_file(path.string());

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
