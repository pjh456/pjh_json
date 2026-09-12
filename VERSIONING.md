# Versioning & Compatibility Policy

`pjh_json` follows [Semantic Versioning 2.0.0](https://semver.org/).
Current version: **0.2.0** (pre-1.0 — see the promise below).

## Source of truth

The version lives in exactly two manual places and they must always match:

| Place | Symbols |
|-------|---------|
| `CMakeLists.txt` | `project(pjh_json VERSION X.Y.Z ...)` |
| `include/pjh_json/version.hpp` | `PJH_JSON_VERSION_MAJOR/MINOR/PATCH`, `PJH_JSON_VERSION_STRING`, `kVersion*` |

A configure-time guard in `CMakeLists.txt` fails the build when they disagree,
and `CMAKE_CONFIGURE_DEPENDS` re-runs it when `version.hpp` changes. The
installed `pjh_jsonConfigVersion.cmake` (what `find_package(pjh_json X.Y)`
consults) is generated from the CMake version, so it cannot drift.

This project is versioned independently of its dependencies. `pjh_result`
(currently 0.2.0) and `xsimd` are pinned as submodules by SHA/tag, not by a
`find_package` version range; a matching number is a convention of the
ecosystem, not a coupling.

## What the compatibility promise covers

- The installed headers: `<pjh_json.hpp>` and `<pjh_json/*.hpp`, **except**
  `<pjh_json/detail/**>` (internal — no promise, may change in any release).
- The installed CMake package, `find_package(pjh_json)`, and the target it
  exports.
- Documented types, functions, macros and behavioral contracts in those
  headers (for example the trailing-NUL padding width, or the error type plus
  `offset()`).

`src/**` and `pjh_json/detail/**` are implementation detail.

## Pre-1.0 compatibility promise (0.y.z)

SemVer §4 says the API of a 0.y.z release is not stable. This project narrows
that to a concrete promise:

- **patch** (`0.y.Z+1`): backwards compatible — bug fixes, documentation and
  build internals only, with no public API/ABI or observable-behavior change.
- **minor** (`0.Y+1.0`): may add features **and may break** public API/ABI or
  documented behavior. Every break is listed under `### Changed` / `### Removed`
  in `CHANGELOG.md` with a one-line migration note.
- **major** (`1.0.0`): the API-stability freeze. From then on the usual SemVer
  contract applies — major = breaking, minor = additive, patch = fixes.

There is deliberately **no deprecation cycle before 1.0.0**: breaks are
announced in the CHANGELOG, not deprecated first. From 1.0.0 on, a removed or
renamed public symbol is marked `[[deprecated]]` for at least one minor release
first, except for security or undefined-behavior fixes.

## `find_package` compatibility

`pjh_jsonConfigVersion.cmake` encodes the boundary above:

- while `major == 0`: `SameMinorVersion` — a consumer must request the `0.y` it
  was written against. `find_package(pjh_json 0.1)` is *not* satisfied by an
  installed `0.2.0`.
- from `1.0.0`: `SameMajorVersion`.

`find_package(pjh_json)` with no version (or a version range) is unaffected.
A consumer that wants to bypass the check simply omits the version.

## Static library / ABI

`pjh_json` is a **static** library: there is no shared-object ABI and no
`SOVERSION`/SONAME today, so consumers always link a local build. ABI
compatibility therefore means "objects compiled against matching headers", and
a minor bump may change `Json` layout or exported symbols — recompile against
the new package. Do not mix headers from one version with a `.a` from another.

If a shared build is ever added, the SONAME must encode `major.minor` while
`major == 0` (otherwise `0.2` and `0.3` share SONAME `0` and a runtime loader
could silently use the wrong one), and plain `major` from 1.0.0 on.

The compiler/standard floor (C++20; GCC ≥ 11, Clang ≥ 16, MSVC ≥ 19.29) is part
of the contract: raising it is a breaking change and bumps the minor version
while pre-1.0.

## Release process

1. Update both version sources (`CMakeLists.txt` and `version.hpp`) in the same
   commit.
2. Move the `[Unreleased]` CHANGELOG entries under a new
   `## [X.Y.Z] - YYYY-MM-DD` heading.
3. Commit (`chore: 发布 vX.Y.Z`).
4. Tag the release commit `vX.Y.Z` and push the branch and the tag.
5. `v0.2.0` is the first release tag in this repository.

## Changelog

`CHANGELOG.md` follows [Keep a Changelog 1.1.0](https://keepachangelog.com/):
newest first, `[Unreleased]` on top, sections `Added` / `Changed` /
`Deprecated` / `Removed` / `Fixed` / `Security`. Entries are one line and
user-observable; internal refactors are omitted.

## Planned breaking changes

- **Result primitives (roadmap 79)**: reworking the error channel to be
  `Result`-first may change or remove the throwing entry points. Scheduled for
  `0.3.0`; it will be recorded under `### Changed` / `### Removed` when merged.
