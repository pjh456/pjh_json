#include <doctest/doctest.h>
#include "pjh_json/json_fwd.hpp" // deliberately the ONLY pjh header
#include <type_traits>

// A true forward/alias header must not drag in a definition header.
#ifdef INCLUDE_PJH_JSON_STRING_HPP
#error "json_fwd.hpp must not include string.hpp"
#endif
#ifdef INCLUDE_PJH_JSON_JSON_HPP
#error "json_fwd.hpp must not include json.hpp"
#endif

using namespace pjh::json;

TEST_CASE("Header: json_fwd is lightweight")
{
    static_assert(std::is_same_v<PathStep,
                                 std::variant<std::string_view, std::size_t>>);
    static_assert(std::is_same_v<Path, std::vector<PathStep>>);
    Path p{std::string_view("k"), std::size_t(0)};
    REQUIRE(p.size() == 2);
}
