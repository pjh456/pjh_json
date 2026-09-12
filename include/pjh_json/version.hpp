#ifndef INCLUDE_PJH_JSON_VERSION_HPP
#define INCLUDE_PJH_JSON_VERSION_HPP

#include <cstdint>
#include <string_view>

// Semantic Versioning 2.0.0. TWO manual sources must agree:
//   * this header (for C++ consumers),
//   * root CMakeLists.txt `project(pjh_json VERSION ...)` (for the CMake
//     package). A configure-time guard fails the build when they diverge.
// Bump all four numbers together and add the matching CHANGELOG entry; see
// VERSIONING.md.
#define PJH_JSON_VERSION_MAJOR 0
#define PJH_JSON_VERSION_MINOR 2
#define PJH_JSON_VERSION_PATCH 0
#define PJH_JSON_VERSION_STRING "0.2.0"

// Packed integer form for numeric comparison: major * 10000 + minor * 100 + patch.
#define PJH_JSON_VERSION_ENCODE(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))
#define PJH_JSON_VERSION                             \
    PJH_JSON_VERSION_ENCODE(                         \
        PJH_JSON_VERSION_MAJOR, PJH_JSON_VERSION_MINOR, PJH_JSON_VERSION_PATCH)

namespace pjh::json
{
    inline constexpr int kVersionMajor = PJH_JSON_VERSION_MAJOR;
    inline constexpr int kVersionMinor = PJH_JSON_VERSION_MINOR;
    inline constexpr int kVersionPatch = PJH_JSON_VERSION_PATCH;
    inline constexpr std::string_view kVersionString = PJH_JSON_VERSION_STRING;
    inline constexpr int kVersion = PJH_JSON_VERSION;
} // namespace pjh::json

#endif // INCLUDE_PJH_JSON_VERSION_HPP
