# pjh_json
高性能与易用性兼顾的 C++ JSON 解析库，聚焦解析速度、低依赖与简洁 API。

## 特性
- SIMD 加速：空白跳过、字符串扫描等关键路径使用 `xsimd` 加速。
- In-situ 解析：字符串内容尽可能原地复用，减少拷贝。
- PMR 友好：`Array`/`Object` 使用 `std::pmr` 容器，便于自定义内存策略。
- API 简洁：`Json` 统一表示 7 种 JSON 类型，索引访问直观。
- 解析可靠：包含字符串转义、Unicode 代理对、错误提示等处理。

## 依赖与环境
- C++20 编译器
- CMake >= 3.15
- `xsimd`（项目内置于 `thirdparty/xsimd`）

## 快速开始

### 1) 作为子项目引入
```cmake
add_subdirectory(pjh_json)
target_link_libraries(your_target PRIVATE pjh::json)
```

### 2) 编译项目
```bash
cmake -S . -B build
cmake --build build
```

### 3) 解析示例
```cpp
#include <pjh_json/json.hpp>

using namespace pjh::json;

int main() {
    // 解析一段字符串（会拷贝并补 64 字节 padding）
    auto doc = parse_copy(R"({"name":"pjh","age":18,"ok":true})");

    // 读取字段
    auto& root = doc;
    auto name = root["name"].as_string();
    auto age = root["age"].as_int();

    // 解析文件
    auto file_doc = parse_file("config.json");
    bool ok = file_doc["ok"].as_boolean();
}
```

## API 概览

### Json 类型与访问
`Json` 内部是 `std::variant`，支持 7 种类型：
`null` / `bool` / `int64_t` / `double` / `string_view` / `Array` / `Object`。

常用接口：
- `is_null/is_boolean/is_int/is_float/is_number/is_string/is_array/is_object`
- `as_boolean/as_int/as_float/as_string/as_array/as_object`
- 数组索引：`j[i]`、`j.at(i)`
- 对象索引：`j["key"]`、`j.at("key")`

注意：
- `Json::at(...)` 在类型不匹配时会抛异常。
- `Object::operator[](key)` 在 key 不存在时会插入空值。
- `Object::operator[](key) const` 在 key 不存在时会抛异常。

### Array / Object
`Array` 和 `Object` 采用 `std::pmr::vector` 存储：
- `Array::Vec` 是 `std::pmr::vector<Json>`
- `Object::Vec` 是 `std::pmr::vector<std::pair<std::string_view, Json>>`

这意味着：
- 查询 key 为线性查找（适合中小对象，换取更低开销）。
- 字符串 key 为 `string_view`，生命周期与底层缓冲区绑定。

## 解析接口与内存模型

### 解析入口
- `parse_copy(json_text, res)`  
  会复制输入文本，并在末尾补齐 **64 字节 '\0' padding** 以支持 SIMD 批量读取。

- `parse_in_situ(buffer, res)`  
  直接在可写 `std::pmr::string` 上原地解析，要求 buffer 已预留 **64 字节 padding**。

- `parse_file(path, res)`  
  读取文件到 buffer 并调用 `parse_in_situ`。

### Document 与生命周期
解析函数返回 `Document`，它继承自 `Json`，并持有用于解析的 buffer：
- `Json` 中的字符串是 `std::string_view`，直接引用 buffer。
- 因此 **必须保证 `Document` 的生命周期长于所有 `Json`/`string_view` 的使用**。

## Benchmark

以下为 2026-03-20（UTC+8）运行结果（Google Benchmark），硬件信息如下：
- CPU：24 x 2304 MHz
- Cache：L1D 48 KiB x12，L1I 32 KiB x12，L2 1280 KiB x12，L3 25600 KiB x1

单位为 **ns**，数值越低越好：

| Case | PJH | Nlohmann | RapidJSON |
| --- | --- | --- | --- |
| 1mb.json | 17,258,964 | 33,933,305 | 7,925,962 |
| 10mb.json | 172,661,700 | 357,890,850 | 81,237,882 |
| 30mb.json | 535,038,550 | 1,208,127,600 | 228,252,667 |
| 50mb.json | 849,329,500 | 1,851,727,700 | 346,825,900 |
| 100mb.json | 1,683,770,600 | 3,674,551,900 | 714,435,400 |

> 数据来自 `benchmarks/pjh_json_benchmark.exe` 的输出（`PJH_JSON_BENCH_DATA_DIR` 指向项目根目录）。

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

注意：Benchmark 会通过 CMake `FetchContent` 拉取 `google/benchmark`、`nlohmann/json`、`rapidjson`。

## 许可
本项目使用 MIT License，详见 `LICENSE`。
