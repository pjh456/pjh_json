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
* **Compile-time JSON**: `ConstJson::of()` builds nested JSON trees at compile time via template type encoding. `ConstJson::parse()` validates JSON strings at compile time. No heap allocation — all data lives inline in `std::tuple`.

> The compile-time API is opt-in: `#include "pjh_json/json_constexpr.hpp"`. The
> runtime umbrella `<pjh_json.hpp>` deliberately does not pull it in.

## Accessors

`Json` has four value-read tracks plus a path/Result surface. Pick by the
question you are answering:

| I want to… | Use | On wrong type | Returns |
|------------|-----|---------------|---------|
| read a known tag (parser/validated data, hot path) | `as_*()` | debug: throws; **release: undefined behavior** | `T&` / `string_view` |
| read a tag and throw on mismatch | `as_*_strict()` | throws `TypeError` (debug + release) | `T&` / `string_view` |
| probe a tag without exceptions | `try_as_*()` | `nullopt` / `nullptr` | `optional<T>` / `T*` |
| read a number, converting if needed | `get<T>()` | throws `TypeError` (debug + release) | `T` |
| probe a number, converting if needed | `try_get<T>()` | `nullopt` | `optional<T>` |
| resolve a field path | `find_path_result()` / `get_path<T>()` | `Result<_, AccessError>` | node / `T` |

`is_*()` (`is_null`, `is_boolean`, `is_int`, `is_integer`, `is_float`,
`is_number`, `is_string`, `is_array`, `is_object`) tests the stored slot
first and is `constexpr`/`noexcept`.

Numeric slots are `Integer` (`int64_t`) and `Floating` (`double`); there is no
`float` slot. `get<T>` / `try_get<T>` accept:

| `T` | Accepted slots | Notes |
|-----|----------------|-------|
| `bool` | Boolean | identity |
| `int64_t` | Integer | a `Floating` source is rejected (narrowing) |
| `float` | Integer, Floating | `int64 → float` rounds (round-to-even) |
| `double` | Integer, Floating | `int64 → double` rounds; values above 2^53 may not round-trip |

`try_as_boolean()` / `try_as_int()` are equivalent to `try_get<bool>()` /
`try_get<int64_t>()` (same signature, same accepted slot). `try_as_float()` is
the `Floating`-slot probe; `try_get<double>()` additionally widens `Integer`.

Numbers are stored in an `int64_t` or `double` slot. Per RFC 8259 §6, this
library bounds the accepted numeric range to a **finite `double`**: a
syntactically valid number whose value overflows to infinity or underflows to
zero (e.g. `1e400`, `1e-400`) is rejected with a positioned `ParseError`
(`"Number out of double range"`), not stored as `inf`/`0`. Integers that fit
`double` but not `int64_t` still parse as `double` (rounded to nearest). The
compile-time `ConstJson::parse()` validator is grammar-only and accepts such
magnitudes (`valid == true`); `to_document()` applies the runtime range gate.

Strings have no node-level `get<T>`: use `as_string()` / `as_string_strict()` /
`try_as_string()`. `get_path<std::string_view>` is the path-based string
channel. To resolve a path, `at_path()` throws, `find_path()` returns
`nullptr`, and `find_path_result()` / `get_path<T>()` return a
`Result<_, AccessError>` carrying the failing field path.

## Example

```cpp
#include <pjh_json.hpp>

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

### JSON5 input mode (partial)

`Config::instance().set_json5(true)` (default `false`) lets the runtime parser
accept a JSON5 1.0.0 subset: `//` and `/* */` comments, one trailing comma per
array/object, single-quoted strings (with the JSON5 string escapes and line
continuations), ASCII unquoted identifier keys, the JSON5 ASCII whitespace
bytes VT/FF, and the JSON5 number spellings (hex `0x`/`0X`, leading `+`,
leading/trailing decimal point, and `Infinity`/`NaN` with an optional `+`/`-`
sign). It does **not** cover Unicode identifiers or Unicode whitespace; that is
a separate follow-up. Numbers beyond int64 fall to a double exactly as on the
RFC path, and a magnitude outside the finite-double range is still rejected
(`Infinity`/`NaN` are accepted only as those exact JSON5 literals).
`dump()` is always RFC 8259, so JSON5 input is normalized on output (comments
dropped, quotes/unquoted keys rewritten, trailing commas removed). A parsed
non-finite value has no RFC 8259 representation, so `dump()` rejects it with a
`JsonError` (`"Cannot serialize non-finite double"`) — a documented round-trip
break. That writer guard is `std::isfinite`, which `-ffast-math` may fold away;
the library deliberately adds no such flag, so do not compile the library with
it. The compile-time `ConstJson::parse()` path stays RFC-only.

### Compile-time JSON construction

```cpp
#include "pjh_json/json_constexpr.hpp"

using namespace pjh::json;

// --- scalars (constexpr) ---
constexpr auto n = to_const_json(42);
static_assert(n.v == 42);
Json jn = to_runtime(n);  // → Json(42)

// --- arrays ---
auto arr = ConstJson::of(1, 2.5, std::string_view("hello"), true, nullptr);
Json jarr = arr.to_runtime();  // → [1,2.5,"hello",true,null]

// --- objects ---
auto obj = ConstJson::of(
    kv("name",  std::string_view("alice")),
    kv("age",   int64_t(30)),
    kv("score", 99.5),
    kv("active", true)
);
Json jobj = obj.to_runtime();

// --- nesting (deeply recursive, all inline) ---
auto root = ConstJson::of(
    kv("user", ConstJson::of(
        kv("id",   42),
        kv("tags", ConstJson::of("admin", "dev"))
    ))
);
Json nested = root.to_runtime();
// → {"user":{"id":42,"tags":["admin","dev"]}}
```

### Compile-time JSON validation

```cpp
// Validates at compile time — invalid JSON is a hard error
constexpr auto pr = ConstJson::parse(R"({"port":8080,"debug":false})");
static_assert(pr.valid);

// Build a Document from validated source at runtime (no re-validation)
auto doc = pr.to_document();
auto port = doc.root()["port"].as_int();  // 8080
```

> **How it works**: `ConstJson::of()` encodes the entire JSON structure as C++ template types. `ConstJson::of(1, 2, 3)` produces `ConstJsonArray<ConstJsonInt, ConstJsonInt, ConstJsonInt>` — each element's type lives in the template parameter pack. Nested containers are embedded in `std::tuple`, so pointers never escape and all data lives on the stack. `to_runtime()` recursively walks the type tree and copies into PMR-backed `Json`. Pure C++20 — no `std::vector`, no heap allocation, no compiler non-transient constexpr support required.

## Requirements

* C++20 (`CXX_STANDARD 20`); configure enforces a compiler floor of GCC >= 11,
  Clang >= 16, AppleClang >= 15, or MSVC >= 19.29.
* CMake >= 3.20.
* Run `git submodule update --init` before the first configure —
  `thirdparty/xsimd` and `thirdparty/pjh_result` are git submodules, not
  FetchContent dependencies.
* Test/benchmark/fuzz dependencies (doctest, Google Benchmark, nlohmann/json,
  RapidJSON) are fetched at configure time by default (`PJH_JSON_DEP_MODE=FETCH`,
  needs network). For offline builds against distro packages use
  `-DPJH_JSON_DEP_MODE=SYSTEM` (Debian/Ubuntu: `libbenchmark-dev
  nlohmann-json3-dev rapidjson-dev`, plus doctest >= 2.5.0 — the distro
  `doctest-dev` may be older than the suite's floor); `AUTO` tries the system
  packages first and falls back to FetchContent.

## Build

`pjh_json` builds a **static library** (`src/*.cpp` compile into it), producing
`libpjh_json.a` (or `pjh_json.lib`); consumers link the target rather than
copying headers.

```bash
git submodule update --init

# library only
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
| `PJH_JSON_DEP_MODE` | `FETCH` | Source for test/benchmark deps: `FETCH` (network), `SYSTEM` (`find_package` only, offline), or `AUTO` (`find_package` first, fetch fallback) |
| `PJH_JSON_PGO` | `OFF` | PGO mode: `GENERATE` or `USE` (requires separate build directories); flags are library-only and tagged PRIVATE, so they are never exported to consumers |
| `PJH_JSON_BUILD_FUZZERS` | `OFF` | Build the libFuzzer differential fuzz targets (Clang only; requires `PJH_JSON_SANITIZERS` to be set) |
| `PJH_JSON_SANITIZERS` | `OFF` | Comma-separated GCC/Clang sanitizer list, e.g. `address,undefined`, or `thread` alone; PRIVATE instrumentation, never exported |

The library injects no optimization or ISA flags of its own — pick the level with the
standard `-DCMAKE_BUILD_TYPE=Release` (or `Debug`). `-march=native` is deliberately not
used (non-portable artifacts); pass your own `CMAKE_CXX_FLAGS` if you want it locally
(x86-64: `-march=native`; arm64/AppleClang: `-mcpu=native` — `-march=native` is
rejected on Apple Silicon; never pass both).

### Formatting & lint

First-party C++ style is defined by the root `.clang-format` (Allman braces,
4-space indent, indented namespace bodies, right-aligned pointers, 120-column
limit). The style is a ratchet: CI only checks the **lines changed** by a
commit/PR, so pre-existing deviations are tolerated but new ones are not.

Pin the toolchain to **clang-format 18.1.8** (`clang-format-18` +
`git-clang-format-18`; on Debian/Ubuntu `apt install clang-format-18`, otherwise
`pip install clang-format==18.1.8`). Formatting output is not stable across
major LLVM versions, so local and CI must match.

```bash
# check only the lines changed relative to main (same mechanism as CI)
base=origin/main
git fetch --no-tags origin main
git-clang-format-18 --diff "$base"

# fix those lines in place, including unstaged files (does not commit)
git-clang-format-18 --force "$base"
```

`.clang-tidy` holds a conservative, report-only baseline (bugprone /
performance / limited modernize); it is **not** a CI gate yet. Run it locally
against a compile-commands database:

```bash
cmake -B build-tidy -G Ninja -S . -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
run-clang-tidy-18 -p build-tidy -quiet
```

### Using the installed package

```cmake
find_package(pjh_json 0.2 REQUIRED)
target_link_libraries(my_app PRIVATE pjh::json)
```

```cpp
#include <pjh_json.hpp>
```

The exported target is namespaced `pjh::json`; the package resolves its pinned
`pjh_result` dependency itself (`find_dependency`). Public headers land in
`<prefix>/include/pjh_json.hpp` + `<prefix>/include/pjh_json/**`, plus the
`pjh_result` headers. The internal `pjh_json/detail/` and the former
`literal.hpp` / `utils.hpp` are implementation details and are not installed. No
optimization, ISA, PGO, or sanitizer flags cross the install boundary.

## Versioning

Current version: **0.2.0**. `pjh_json` follows Semantic Versioning; because it
is pre-1.0, a minor-version bump may be breaking. See
[VERSIONING.md](VERSIONING.md) for the compatibility promise and
[CHANGELOG.md](CHANGELOG.md) for release notes.

## Benchmark

> Historical numbers (2026-07-10, Windows x64, MinGW GCC 15.2), captured with an
> optimization/ISA flag set that the library has since removed; LTO is
> intentionally disabled. See [Build](#build) for the current flag policy.
> Time in nanoseconds, lower is better.

Inputs (`1mb.json` … `1gb.json`) are generated on first run under
`<build>/benchmarks/data`; set `PJH_JSON_BENCH_DATA_DIR` to share one cache
across build trees, and remove them with
`cmake --build <build> --target pjh_json_benchmark_clean_data`.

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
* **In-situ + padding**: SIMD wide loads overread, so buffers carry `kPaddingWidth` (= 128, a fixed 2x the maximum supported SIMD batch width) trailing NUL bytes. `parse_copy`/`parse_file` pad automatically; `parse_in_situ`/`parse_view` require the caller to pad.
* **Ownership**: `Document` owns the arena, the raw buffer, and the root value. Borrowed views stay valid only while the `Document` is alive.
* **PMR arenas**: allocation policy is `Pooled` (default), `Arena`, or `SystemDefault`, selectable per parse or globally via `Config`.
* **Fast/slow split**: the hot path stays branch-light; escapes, `\uXXXX`, and non-finite doubles fall to rare slow paths.

## License

MIT. See [LICENSE](LICENSE).
