# pjh_json

SIMD-accelerated C++20 JSON parser. Custom tagged-union `Json` type (24 bytes), PMR-based memory management, zero-copy strings.

`pjh_json` is ~5x faster than `Nlohmann` across all sizes, and matches `RapidJSON` at scale (faster at 1 GB, tied at 500 MB).

## Features

* **SIMD parsing**: whitespace skip and string scan via `xsimd`
* **Custom tagged union**: `Json` is 24 bytes (vs std::variant 48 bytes)
* **Zero-copy strings**: in-situ parse borrows from input buffer, no copy
* **PMR allocators**: choose `Pooled` (default), `Arena`, or `SystemDefault`
* **Parse modes**: `parse_copy`, `parse_in_situ`, `parse_view`, `parse_file`, `parse_jsonl`
* **Serialization**: `dump` (compact/pretty), `dump_jsonl`, `prettify`
* **Safe access**: `try_as_int()`, `try_as_string()`, `try_as_array()`, etc. -- returns `nullopt` on type mismatch
* **Builder API**: `Array::of(...)`, `Object::of(...)` for in-code construction
* **Compile-time construction**: `constexpr` scalars and `String` views; `consteval` array/object factories with runtime bridge. *Array/Object bridges copy into PMR — not zero-cost.*

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

### Compile-time construction

```cpp
#include "pjh_json/json.hpp"
#include "pjh_json/document.hpp"   // literal factories & bridges

using namespace pjh::json;

// Scalars and strings are zero-cost — constructed entirely at compile time
constexpr Json count = int64_t(42);
constexpr Json label = std::string_view("hello");
constexpr String key   = "name";
static_assert(count.is_int());
static_assert(label == std::string_view("hello"));

// Arrays/objects: compile-time factory + runtime bridge (copies into PMR)
auto arr = array_from_literal(
    json_array_literal(1, 2, std::string_view("three"), false, nullptr));

auto obj = object_from_literal(std::array{
    kv("name", std::string_view("alice")),
    kv("age", int64_t(30)),
    kv("active", true),
});

std::pmr::string out = dump(Json(std::move(arr)));
// arr is [1,2,"three",false,null]
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
| `PJH_JSON_ENABLE_OPTIMIZATIONS` | `ON` | Apply `-O3 -march=native -ffast-math` (GCC/Clang) or `/O2 /arch:AVX2` (MSVC), plus LTO |
| `PJH_JSON_PGO` | `OFF` | PGO mode: `GENERATE` or `USE` (requires separate build directories) |

## Benchmark

Google Benchmark, 2026-07-10, Windows x64, MinGW GCC 15.2, `-O3 -march=native -ffast-math -flto`. Time in nanoseconds, lower is better.

| File | `pjh_json` | `Nlohmann` | `RapidJSON` | vs `Nlohmann` | vs `RapidJSON` |
|------|----------|----------|-----------|-------------|--------------|
| 1mb | 7,718,335 | 37,683,824 | 5,000,000 | 4.9x faster | 1.5x slower |
| 10mb | 73,783,811 | 376,909,950 | 51,146,440 | 5.1x faster | 1.4x slower |
| 30mb | 204,950,667 | 1,108,532,900 | 149,929,325 | 5.4x faster | 1.4x slower |
| 50mb | 359,449,850 | 1,818,289,800 | 253,719,533 | 5.1x faster | 1.4x slower |
| 100mb | 682,962,700 | 3,671,994,300 | 616,983,000 | 5.4x faster | 1.1x slower |
| 200mb | 1,519,806,900 | 7,611,437,200 | 1,063,263,900 | 5.0x faster | 1.4x slower |
| 500mb | 4,202,217,200 | 19,909,000,000 | 4,161,192,600 | 4.7x faster | 1.0x (tie) |
| 1gb | 9,252,783,000 | 44,975,000,000 | 10,260,000,000 | 4.9x faster | 1.1x faster |

## Design

* **Tagged union**: `Json` is a hand-rolled tagged union, 24 bytes. Small types (null/bool/int64/double/borrowed string) live inline; heap types (owned string/`Array`/`Object`) are PMR-allocated pointers.
* **Dual string storage**: parsed strings are borrowed `string_view` {ptr, len} into the buffer; only strings needing unescaping are promoted to an owned `pmr::string`.
* **In-situ + padding**: SIMD wide loads overread, so buffers carry 64 trailing NUL bytes. `parse_copy`/`parse_file` pad automatically; `parse_in_situ`/`parse_view` require the caller to pad.
* **Ownership**: `Document` owns the arena, the raw buffer, and the root value. Borrowed views stay valid only while the `Document` is alive.
* **PMR arenas**: allocation policy is `Pooled` (default), `Arena`, or `SystemDefault`, selectable per parse or globally via `Config`.
* **Fast/slow split**: the hot path stays branch-light; escapes, `\uXXXX`, and non-finite doubles fall to rare slow paths.

## License

MIT. See [LICENSE](LICENSE).
