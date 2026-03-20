# pjh_json（中文文档）

一个兼顾高性能与易用性的 C++ JSON 解析库，提供 SIMD 加速、零拷贝字符串、
原地解析与 PMR 友好容器。

## 特性
- SIMD 加速空白跳过与字符串扫描
- `std::string_view` 零拷贝字符串
- 原地解析，输入末尾 64 字节 padding
- PMR 容器，便于自定义内存策略
- 简洁 DOM API：`Json`/`Array`/`Object`

## 依赖与环境
- C++20 编译器
- CMake 3.15+
- `xsimd`（项目内置在 `thirdparty/xsimd`）

## 构建
```bash
cmake -S . -B build
cmake --build build
```

## 使用示例
```cpp
#include <pjh_json/json.hpp>

using namespace pjh::json;

int main() {
    auto doc = parse_copy(R"({"name":"pjh","age":18,"ok":true})");
    auto& root = doc;
    auto name = root["name"].as_string();
    auto age = root["age"].as_int();

    auto file_doc = parse_file("config.json");
    bool ok = file_doc["ok"].as_boolean();
}
```

## API 概览
`Json` 是一个 `std::variant`，支持 7 种 JSON 类型：
`null` / `bool` / `int64_t` / `double` / `string_view` / `Array` / `Object`。

常用接口：
- `is_null/is_boolean/is_int/is_float/is_number/is_string/is_array/is_object`
- `as_boolean/as_int/as_float/as_string/as_array/as_object`
- 数组索引：`j[i]`、`j.at(i)`
- 对象索引：`j["key"]`、`j.at("key")`

注意：
- `Json::at(...)` 在类型不匹配时会抛异常。
- `Object::operator[](key)` 在 key 不存在时会插入 `Json()`。
- `Object::operator[](key) const` 在 key 不存在时会抛异常。

## 解析接口与内存模型
解析入口：
- `parse_copy(json_text, res)` 复制输入并追加 64 字节 `\0` padding
- `parse_in_situ(buffer, res)` 原地解析，buffer 需已 padding
- `parse_file(path, res)` 读取文件后原地解析

`Document` 持有底层 buffer，字符串为 `std::string_view`：
- `Document` 析构后字符串视图失效，请确保生命周期正确。

## Benchmark
2026-03-20（UTC+8）基准测试结果，数值越低越好：

| Case | PJH | Nlohmann | RapidJSON |
| --- | --- | --- | --- |
| 1mb.json | 17,258,964 ns | 33,933,305 ns | 7,925,962 ns |
| 10mb.json | 172,661,700 ns | 357,890,850 ns | 81,237,882 ns |
| 30mb.json | 535,038,550 ns | 1,208,127,600 ns | 228,252,667 ns |
| 50mb.json | 849,329,500 ns | 1,851,727,700 ns | 346,825,900 ns |
| 100mb.json | 1,683,770,600 ns | 3,674,551,900 ns | 714,435,400 ns |

## 运行测试
```bash
cmake -S . -B build -DPJH_JSON_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

## 运行 Benchmark
```bash
cmake -S . -B build -DPJH_JSON_BUILD_BENCHMARKS=ON
cmake --build build
./build/benchmarks/pjh_json_benchmark
```

说明：
- Benchmark 通过 CMake `FetchContent` 下载依赖。

## 许可
MIT License，详见 [LICENSE](../LICENSE)。
