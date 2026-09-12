#include <doctest/doctest.h>
#include <pjh_json.hpp> // deliberately the ONLY pjh header
#include <ostream>

// The umbrella must stay runtime-only: json_constexpr.hpp / validate.hpp are
// opt-in. Their include guards must be undefined after including the umbrella.
#ifdef INCLUDE_PJH_JSON_CONSTEXPR_HPP
#error "umbrella pjh_json.hpp must not include json_constexpr.hpp"
#endif
#ifdef INCLUDE_PJH_JSON_VALIDATE_HPP
#error "umbrella pjh_json.hpp must not include validate.hpp"
#endif

using namespace pjh::json;

TEST_CASE("Header: umbrella is runtime-only")
{
    auto doc = parse_copy(R"({"a":1,"b":[true,null]})");
    REQUIRE(doc.root()["a"].as_int() == (int64_t)1);
    REQUIRE(doc.root()["b"].as_array().size() == 2);
    REQUIRE(dump(doc) == std::string_view(R"({"a":1,"b":[true,null]})"));
}
