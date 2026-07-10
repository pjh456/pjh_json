#include "pjh_json/parser.hpp"
#include <xsimd/xsimd.hpp>

namespace pjh::json
{
    // Count consecutive backslash chars ending at p (read backwards).
    static uint32_t count_trailing_backslashes(const char *p, const char *limit)
    {
        uint32_t n = 0;
        while (p >= limit && *p == '\\')
        {
            ++n;
            --p;
        }
        return n;
    }

    /*
     * Build structural character index (Stage 1 of two-stage parsing).
     *
     * 1. SIMD scan for structural bytes: " , : [ ] { }
     * 2. Record offset + char for each match.
     * 3. Filter escaped quotes: any '"' preceded by odd '\' count is
     *    zeroed out (type=0) so Stage 2 skips it.
     */
    Stage1Index build_structural_index(
        const char *data, size_t len,
        std::pmr::memory_resource *res)
    {
        Stage1Index idx;
        idx.offsets = std::pmr::vector<uint32_t>(res);
        idx.types = std::pmr::vector<uint8_t>(res);

        using batch_type = xsimd::batch<uint8_t>;
        constexpr std::size_t batch_size = batch_type::size;
        constexpr uint64_t full_mask = (batch_size < 64)
                                           ? (1ULL << batch_size) - 1
                                           : ~0ULL;

        auto quote = xsimd::broadcast<uint8_t>('"');
        auto comma = xsimd::broadcast<uint8_t>(',');
        auto colon = xsimd::broadcast<uint8_t>(':');
        auto lbrack = xsimd::broadcast<uint8_t>('[');
        auto rbrack = xsimd::broadcast<uint8_t>(']');
        auto lbrace = xsimd::broadcast<uint8_t>('{');
        auto rbrace = xsimd::broadcast<uint8_t>('}');

        size_t pos = 0;

        while (pos < len)
        {
            auto b = batch_type::load_unaligned(
                reinterpret_cast<const uint8_t *>(data + pos));

            auto structural = (b == quote) | (b == comma) | (b == colon) |
                              (b == lbrack) | (b == rbrack) |
                              (b == lbrace) | (b == rbrace);

            uint64_t mask = structural.mask();
            if constexpr (batch_size < 64)
                mask &= full_mask;

            while (mask)
            {
                int bit = std::countr_zero(mask);
                size_t p = pos + bit;
                if (p >= len)
                    break;
                idx.offsets.push_back(static_cast<uint32_t>(p));
                idx.types.push_back(static_cast<uint8_t>(data[p]));
                mask &= mask - 1;
            }

            pos += batch_size;
        }

        // Filter escaped quotes
        for (size_t i = 0; i < idx.types.size(); ++i)
        {
            if (idx.types[i] != '"')
                continue;
            uint32_t offset = idx.offsets[i];
            if (offset > 0 &&
                count_trailing_backslashes(data + offset - 1, data) % 2 == 1)
            {
                idx.types[i] = 0;
            }
        }

        return idx;
    }
}
