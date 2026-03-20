# pjh_json
SIMD-accelerated C++ JSON parser that balances raw speed with a clean, modern API.

[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/CMake-3.15%2B-064F8C)](https://cmake.org/)
[![License: MIT](https://img.shields.io/badge/License-MIT-green)](https://opensource.org/licenses/MIT)

## Why pjh_json
- Fast parsing with SIMD on the hottest paths (whitespace and string scans)
- Zero-copy strings via `std::string_view` and in-situ parsing
- PMR-ready containers for predictable allocation and teardown
- Minimal dependencies with `xsimd` vendored in `thirdparty`

## Performance Snapshot
Google Benchmark results on 2026-03-20 (UTC+8), lower is better:

| Case | PJH | Nlohmann | RapidJSON |
| --- | --- | --- | --- |
| 1mb.json | 17,258,964 ns | 33,933,305 ns | 7,925,962 ns |
| 10mb.json | 172,661,700 ns | 357,890,850 ns | 81,237,882 ns |
| 100mb.json | 1,683,770,600 ns | 3,674,551,900 ns | 714,435,400 ns |

## Documentation
- [Docs (EN)](docs/readme_en.md)
- [文档（中文）](docs/readme_zh.md)
- [Design Notes (EN)](docs/design/en)
- [设计说明（中文）](docs/design/zh)

## Design Highlights
- SIMD scanning with 64-byte padding for safe wide loads
- In-place construction to avoid temporaries
- Fast/slow path split to keep the hot path branch-light
- Ownership centralized in `Document` for correct zero-copy lifetimes

## License
MIT License. See [LICENSE](LICENSE).
