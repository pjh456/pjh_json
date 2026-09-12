#ifndef INCLUDE_PJH_JSON_FWD_HPP
#define INCLUDE_PJH_JSON_FWD_HPP

#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

namespace pjh::json
{
    class Json;
    struct ConstJson;

    /**
     * @brief One hop of a Json path: object key or array index
     * @note The variant alternative IS the hop kind — no ambiguity.
     *       A string_view key borrows the path string's lifetime.
     */
    using PathStep = std::variant<std::string_view, std::size_t>;

    /**
     * @brief Typed Json path (sequence of hops)
     * @note Plain std::vector (not pmr): the path is short-lived
     *       (parse-and-walk). Key string_views borrow the source path
     *       string — keep that string alive as long as the Path is used.
     */
    using Path = std::vector<PathStep>;
}

#endif // INCLUDE_PJH_JSON_FWD_HPP