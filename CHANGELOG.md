# Changelog

All notable changes to `pjh_json` are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and this project
adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).
See [VERSIONING.md](VERSIONING.md) for the compatibility promise.

## [Unreleased]

### Changed

- (roadmap 79, planned) error channel moves to `Result`-first; throwing entry
  points may change or be removed in `0.3.0`.

## [0.2.0] - 2026-09-12

First SemVer release; the project previously used the non-SemVer string
`VERSION 0.01`. This entry is a condensed summary — the complete record lives
in `.w1mer/ROADMAP.md` and the review archive.

### Added

- Structured error entry points (`parse_*_result` / `dump_*_result`) built on
  the external `pjh_result` library.
- Path lookup: `at_path` / `find_path` / `find_path_result` / `get_path<T>` and
  the typed `Path` / `parse_path` DSL.
- Checked numeric conversion `get<T>` / `try_get<T>`; strict accessors
  `as_*_strict`; `visit` / `as_variant`.
- `Json` range-for, `Object::keys()` / `values()`, `std::hash<Json>`,
  `Json::operator<`, `Object::merge`.
- `dump_to(std::string&)`, `parse(std::istream&)`, streaming file I/O.
- Optional BOM stripping and strict UTF-8 validation; `to_std` / `from_std`.
- CMake install/export package with `find_package(pjh_json)`.

### Changed

- Public install surface narrowed to the public headers — `pjh_json/detail/`
  is internal and not installed.
- Duplicate object keys are last-wins (single entry per key).
- A default recursion-depth limit of 512 applies to parse and dump.
- Numbers outside the finite-`double` range are rejected with a positioned
  `ParseError`.
- `kPaddingWidth` is a fixed 128 (`2 x` the maximum SIMD batch width).

### Fixed

- A series of correctness, lifetime and UB fixes (move-assign UAF, moved-from
  state, padding validation, error offsets, low-level UB, large-object O(N^2)
  hot paths). See the task archive and git history.
