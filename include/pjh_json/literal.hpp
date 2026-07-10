#ifndef INCLUDE_PJH_JSON_LITERAL_HPP
#define INCLUDE_PJH_JSON_LITERAL_HPP

#include "json.hpp"

#include <array>
#include <bit>

namespace pjh::json
{

    // bit_cast magic for "true" / "false" / "null" literal matching
    inline constexpr uint32_t kTrueMagic = std::bit_cast<uint32_t>(
        std::array<uint8_t, 4>{'t', 'r', 'u', 'e'});
    inline constexpr uint32_t kNullMagic = std::bit_cast<uint32_t>(
        std::array<uint8_t, 4>{'n', 'u', 'l', 'l'});
    inline constexpr uint64_t kFalseMagic = std::bit_cast<uint64_t>(
        std::array<uint8_t, 8>{'f', 'a', 'l', 's', 'e', 0, 0, 0});
    inline constexpr uint64_t kFalseMask = std::bit_cast<uint64_t>(
        std::array<uint8_t, 8>{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0});

    // compile-time literal factories
    template <typename... Ts>
    consteval auto json_array_literal(Ts &&...vals)
    {
        return std::array<Json, sizeof...(Ts)>{Json(std::forward<Ts>(vals))...};
    }

    consteval auto kv(const char *key, auto &&val)
    {
        return Object::Entry{String(key), Json(val)};
    }

    // runtime bridges: copy from compile-time array into PMR-backed container
    template <size_t N>
    inline Array array_from_literal(
        std::array<Json, N> arr,
        std::pmr::memory_resource *res = Config::instance().resource())
    {
        Array a(res);
        a.reserve(N);
        for (auto &el : arr)
            a.push_back(std::move(el));
        return a;
    }

    template <size_t N>
    inline Object object_from_literal(
        std::array<Object::Entry, N> entries,
        std::pmr::memory_resource *res = Config::instance().resource())
    {
        Object o(res);
        for (auto &e : entries)
            o.insert(std::move(e));
        return o;
    }

}

#endif
