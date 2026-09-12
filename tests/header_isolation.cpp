#include <doctest/doctest.h>
#include <ostream>
#include <pjh_json/json.hpp> // deliberately no document.hpp / pjh_json.hpp

using namespace pjh::json;

TEST_CASE("Header: json.hpp alone provides Array::of/Object::of")
{
    Array arr = Array::of(Json((int64_t)1), Json("two"), Json(true));
    REQUIRE(arr.size() == 3);
    REQUIRE(arr[0].as_int() == (int64_t)1);
    REQUIRE(arr[1].as_string() == std::string_view("two"));

    Object obj = Object::of(Object::Entry{"k", Json((int64_t)1)},
                            Object::Entry{"s", Json("v")});
    REQUIRE(obj.size() == 2);
    REQUIRE(obj.at("k").as_int() == (int64_t)1);
}
