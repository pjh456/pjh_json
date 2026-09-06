#ifndef INCLUDE_PJH_JSON_UTILS_HPP
#define INCLUDE_PJH_JSON_UTILS_HPP

#include "error.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

#include <xsimd/xsimd.hpp>

namespace pjh::json
{

    /**
     * @brief Required trailing NUL padding for padded parse entry points
     *
     * SIMD wide loads (xsimd::batch<uint8_t>, width 16/32/64 by target ISA)
     * may read up to one full batch past the logical end of the content.
     * The contract carries 2x that width of headroom (compile-time derived,
     * so an ISA widening keeps the 2x invariant automatically), so
     * parse_in_situ/parse_view require at least this many NUL bytes beyond
     * the content; parse_copy/parse_file/parse_jsonl pad automatically.
     * parse_in_situ verifies the tail at runtime; parse_view cannot (a check
     * would itself overread) - that is a hard caller contract.
     * @note If the library and a caller TU are built with different arch
     * flags, their batch widths differ; pad with the wider of the two.
     */
    inline constexpr size_t kPaddingWidth = 2 * xsimd::batch<uint8_t>::size;

#ifdef NDEBUG
#define PJH_JSON_NOEXCEPT noexcept

    template <typename T>
    constexpr void debug_check_type(T, T, const char *) noexcept
    {
    }

    template <typename T>
    constexpr void debug_check_type2(T, T, T, const char *) noexcept
    {
    }

#else
#define PJH_JSON_NOEXCEPT

    template <typename T>
    inline void debug_check_type(T actual, T expected, const char *name)
    {
        if (actual != expected)
            throw TypeError(
                std::string("type mismatch in as_") + name + "()");
    }

    template <typename T>
    inline void debug_check_type2(T actual, T a, T b, const char *name)
    {
        if (actual != a && actual != b)
            throw TypeError(
                std::string("type mismatch in as_") + name + "()");
    }

#endif

    [[noreturn]] inline void throw_parse_error(const char *msg, const char *curr, const char *begin)
    {
        auto off = static_cast<size_t>(curr - begin);
        throw ParseError(std::string(msg) + " at offset " + std::to_string(off), off);
    }

    uint32_t parse_hex4(const char *&curr, const char *begin);
    void encode_utf8(uint32_t cp, char *&dst);
    void handle_escape(char *&dst, const char *&m_curr, const char *m_begin);

}

#endif
