#include <doctest/doctest.h>
#include "pjh_json/json_constexpr.hpp" // deliberately no umbrella / json.hpp first

using namespace pjh::json;

TEST_CASE("Header: json_constexpr opt-in is self-sufficient")
{
    constexpr auto pr = ConstJson::parse(R"({"n":1,"a":[true,null]})");
    static_assert(pr.valid);
    auto doc = pr.to_document();
    REQUIRE(doc.root()["n"].get<int64_t>() == (int64_t)1);

    auto cj = ConstJson::of(kv("n", int64_t(1)),
                            kv("a", ConstJson::of(true, nullptr)));
    Json j = cj.to_runtime();
    REQUIRE(j["n"].as_int() == (int64_t)1);
    REQUIRE(j["a"].as_array().size() == 2);
}
