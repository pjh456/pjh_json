#ifndef INCLUDE_PJH_JSON_LITERAL_HPP
#define INCLUDE_PJH_JSON_LITERAL_HPP

#include <array>
#include <bit>
#include <cstdint>

namespace pjh::json
{

    // bit_cast magic for "true" / "false" / "null" literal matching (used by runtime parser)
    inline constexpr uint32_t kTrueMagic = std::bit_cast<uint32_t>(
        std::array<uint8_t, 4>{'t', 'r', 'u', 'e'});
    inline constexpr uint32_t kNullMagic = std::bit_cast<uint32_t>(
        std::array<uint8_t, 4>{'n', 'u', 'l', 'l'});
    inline constexpr uint64_t kFalseMagic = std::bit_cast<uint64_t>(
        std::array<uint8_t, 8>{'f', 'a', 'l', 's', 'e', 0, 0, 0});
    inline constexpr uint64_t kFalseMask = std::bit_cast<uint64_t>(
        std::array<uint8_t, 8>{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0});

} // namespace pjh::json

#endif
