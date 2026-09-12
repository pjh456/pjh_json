#ifndef INCLUDE_PJH_JSON_UTILS_HPP
#define INCLUDE_PJH_JSON_UTILS_HPP

#include "error.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace pjh::json
{

    /**
     * @brief Required trailing NUL padding for padded parse entry points
     *
     * SIMD wide loads read up to one full uint8 batch past the logical end of
     * the content. The implementation caps that batch at 64 bytes (the lane
     * mask is a uint64_t), so this contract reserves a fixed 2x that ceiling
     * (128 NUL bytes). The value is ISA-independent and therefore identical
     * in the library and every consumer TU. parse_in_situ/parse_view require
     * at least this many NUL bytes beyond the content; parse_copy/parse_file/
     * parse_jsonl pad automatically. parse_in_situ verifies the tail at
     * runtime; parse_view cannot (a check would itself overread) - that is a
     * hard caller contract.
     */
    inline constexpr size_t kPaddingWidth = 128;

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

    /**
     * @brief Both-modes type guard for the as_*_strict family
     *
     * Gateless twin of debug_check_type: throws TypeError in debug AND
     * release (as_*'s check compiles away under NDEBUG — a mismatched
     * call there reads an inactive union member, the UB class the
     * _strict family closes). Message keeps the house format
     * "type mismatch in as_<name>()"; the name argument carries the
     * "_strict" suffix so the message names the calling function.
     */
    template <typename T>
    inline void check_type_strict(T actual, T expected, const char *name)
    {
        if (actual != expected)
            throw TypeError(
                std::string("type mismatch in as_") + name + "()");
    }

    /**
     * @brief Both-modes two-tag variant (the string twins accept
     *        StringView and StringOwned)
     */
    template <typename T>
    inline void check_type_strict2(T actual, T a, T b, const char *name)
    {
        if (actual != a && actual != b)
            throw TypeError(
                std::string("type mismatch in as_") + name + "()");
    }

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
