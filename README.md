# pjh_json

SIMD-accelerated C++20 JSON parser. Custom tagged-union `Json` type (24 bytes), PMR-based memory management, zero-copy strings.

`pjh_json` is ~5x faster than `Nlohmann` across all sizes. Against `RapidJSON`, `pjh_json` trails ~30-55% up to 500 MB but pulls ahead at 1 GB.

## Features

* **SIMD parsing**: whitespace skip and string scan via `xsimd`
* **Custom tagged union**: `Json` is 24 bytes (vs std::variant 48 bytes)
* **Zero-copy strings**: in-situ parse borrows from input buffer, no copy
* **PMR allocators**: choose `Pooled` (default), `Arena`, or `SystemDefault`
* **Parse modes**: `parse_copy`, `parse_in_situ`, `parse_view`, `parse_file`, `parse_jsonl`
* **Serialization**: `dump` (compact/pretty), `dump_jsonl`, `prettify`
* **Safe access**: `try_as_int()`, `try_as_string()`, `try_as_array()`, etc. -- returns `nullopt` on type mismatch
* **Builder API**: `Array::of(...)`, `Object::of(...)` for in-code construction

## Example

```cpp
#include "pjh_json/json.hpp"

using namespace pjh::json;

// Parse
auto doc = parse_copy(R"({"name":"pjh_json","version":1})");
const auto& root = doc.root();

// Access
auto ver = root["version"].try_as_int();  // std::optional<int64_t>
auto name = root["name"].try_as_string(); // std::optional<std::string_view>

// Build
Object obj = Object::of(
    Object::Entry{"key", "value"},
    Object::Entry{"count", 42},
    Object::Entry{"tags", Array::of("a", "b", "c")}
);

// Serialize
std::pmr::string compact = dump(doc);
std::pmr::string pretty  = dump(doc, DumpOptions{.pretty = true, .indent = 2});
dump_file("out.json", doc.root());
```

## Build

```bash
# header-only
cmake -B build -S . -G Ninja

# with tests, examples, benchmarks
cmake -B build -S . -G Ninja \
  -DPJH_JSON_BUILD_TESTS=ON \
  -DPJH_JSON_BUILD_EXAMPLES=ON \
  -DPJH_JSON_BUILD_BENCHMARKS=ON

cmake --build build
```

### Build options

| Option | Default | Description |
|--------|---------|-------------|
| `PJH_JSON_BUILD_TESTS` | `OFF` | Build unit tests |
| `PJH_JSON_BUILD_EXAMPLES` | `OFF` | Build example programs |
| `PJH_JSON_BUILD_BENCHMARKS` | `OFF` | Build Google Benchmark suite |
| `PJH_JSON_ENABLE_OPTIMIZATIONS` | `ON` | Apply `-O3 -march=native` (GCC/Clang) or `/O2 /arch:AVX2` (MSVC) |

## Benchmark

Google Benchmark, 2026-07-10, Windows x64, MSVC, `/O2 /arch:AVX2`. Time in nanoseconds, lower is better.

| File | `pjh_json` | `Nlohmann` | `RapidJSON` | vs `Nlohmann` | vs `RapidJSON` |
|------|----------|----------|-----------|-------------|--------------|
| 1mb | 7,478,602 | 40,866,416 | 5,629,413 | 5.5x faster | 1.3x slower |
| 10mb | 75,769,567 | 398,856,250 | 54,972,610 | 5.3x faster | 1.4x slower |
| 30mb | 212,723,967 | 1,151,613,100 | 165,299,550 | 5.4x faster | 1.3x slower |
| 50mb | 357,752,000 | 1,892,383,800 | 268,116,950 | 5.3x faster | 1.3x slower |
| 100mb | 692,498,000 | 3,877,429,500 | 524,787,400 | 5.6x faster | 1.3x slower |
| 200mb | 1,529,318,100 | 7,728,438,800 | 1,093,066,900 | 5.1x faster | 1.4x slower |
| 500mb | 4,418,134,700 | 24,716,000,000 | 2,889,175,500 | 5.6x faster | 1.5x slower |
| 1gb | 9,265,859,800 | 45,631,000,000 | 10,436,000,000 | 4.9x faster | 1.1x faster |

## Design

* **Tagged union**: `Json` is a hand-rolled tagged union, 24 bytes. Small types (null/bool/int64/double/borrowed string) live inline; heap types (owned string/`Array`/`Object`) are PMR-allocated pointers.
* **Dual string storage**: parsed strings are borrowed `string_view` {ptr, len} into the buffer; only strings needing unescaping are promoted to an owned `pmr::string`.
* **In-situ + padding**: SIMD wide loads overread, so buffers carry 64 trailing NUL bytes. `parse_copy`/`parse_file` pad automatically; `parse_in_situ`/`parse_view` require the caller to pad.
* **Ownership**: `Document` owns the arena, the raw buffer, and the root value. Borrowed views stay valid only while the `Document` is alive.
* **PMR arenas**: allocation policy is `Pooled` (default), `Arena`, or `SystemDefault`, selectable per parse or globally via `Config`.
* **Fast/slow split**: the hot path stays branch-light; escapes, `\uXXXX`, and non-finite doubles fall to rare slow paths.

## License

MIT. See [LICENSE](LICENSE).
