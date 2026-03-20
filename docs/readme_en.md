# pjh_json (English Documentation)

High-performance and easy-to-use C++ JSON parser with SIMD acceleration,
zero-copy strings, and PMR-friendly containers.

## Features
- SIMD acceleration for whitespace skipping and string scanning
- Zero-copy strings using `std::string_view`
- In-situ parsing with 64-byte padding
- PMR containers for predictable allocations
- Clean DOM API with `Json`, `Array`, and `Object`

## Requirements
- C++20 compiler
- CMake 3.15+
- `xsimd` (vendored under `thirdparty/xsimd`)

## Build
```bash
cmake -S . -B build
cmake --build build
```

## Usage
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

## API Overview
`Json` is a `std::variant` with 7 JSON types:
`null` / `bool` / `int64_t` / `double` / `string_view` / `Array` / `Object`.

Common helpers:
- `is_null/is_boolean/is_int/is_float/is_number/is_string/is_array/is_object`
- `as_boolean/as_int/as_float/as_string/as_array/as_object`
- Array access: `j[i]`, `j.at(i)`
- Object access: `j["key"]`, `j.at("key")`

Notes:
- `Json::at(...)` throws on type mismatch.
- `Object::operator[](key)` inserts `Json()` if missing.
- `Object::operator[](key) const` throws if missing.

## Parsing APIs and Memory Model
Parsing entry points:
- `parse_copy(json_text, res)` copies input and adds 64-byte `\0` padding
- `parse_in_situ(buffer, res)` parses in-place, buffer must already be padded
- `parse_file(path, res)` loads file and parses in-situ

`Document` owns the backing buffer and returns `std::string_view`:
- Keep the `Document` alive as long as any string views are used.

## Benchmarks
Google Benchmark results on 2026-03-20 (UTC+8), lower is better:

| Case | PJH | Nlohmann | RapidJSON |
| --- | --- | --- | --- |
| 1mb.json | 17,258,964 ns | 33,933,305 ns | 7,925,962 ns |
| 10mb.json | 172,661,700 ns | 357,890,850 ns | 81,237,882 ns |
| 30mb.json | 535,038,550 ns | 1,208,127,600 ns | 228,252,667 ns |
| 50mb.json | 849,329,500 ns | 1,851,727,700 ns | 346,825,900 ns |
| 100mb.json | 1,683,770,600 ns | 3,674,551,900 ns | 714,435,400 ns |

## Tests
```bash
cmake -S . -B build -DPJH_JSON_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build
```

## Benchmarks (Build and Run)
```bash
cmake -S . -B build -DPJH_JSON_BUILD_BENCHMARKS=ON
cmake --build build
./build/benchmarks/pjh_json_benchmark
```

Notes:
- Benchmark uses CMake `FetchContent` to download dependencies.

## License
MIT License. See [LICENSE](../LICENSE).
