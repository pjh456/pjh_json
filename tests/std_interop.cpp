#include <doctest/doctest.h>
#include <ostream>

#include <pjh_json/document.hpp>
#include <pjh_json/json.hpp>
#include <pjh_json/std_interop.hpp>
#include <pjh_json/writer.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

using namespace pjh::json;

namespace
{
    // Local counting resource (the library's src/counting_resource.hpp is
    // private — not on the test include path).
    struct TestCountingResource : std::pmr::memory_resource
    {
        explicit TestCountingResource()
            : m_up(std::make_unique<std::pmr::unsynchronized_pool_resource>()) {}

        [[nodiscard]] long long outstanding() const noexcept { return m_outstanding; }

    protected:
        void *do_allocate(std::size_t n, std::size_t align) override
        {
            ++m_outstanding;
            return m_up->allocate(n, align);
        }
        void do_deallocate(void *p, std::size_t n, std::size_t align) override
        {
            --m_outstanding;
            m_up->deallocate(p, n, align);
        }
        bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override
        {
            return this == &other;
        }

    private:
        std::unique_ptr<std::pmr::memory_resource> m_up;
        long long m_outstanding = 0;
    };
}

TEST_CASE("Interop: to_std scalar mapping") {
    StdValue v_null = to_std(Json(nullptr));
    REQUIRE(std::holds_alternative<std::monostate>(v_null.data));

    StdValue v_bool = to_std(Json(true));
    REQUIRE(std::holds_alternative<bool>(v_bool.data));
    REQUIRE(std::get<bool>(v_bool.data) == true);

    // Integer maps by slot to int64_t — never widened into double.
    StdValue v_int = to_std(Json((int64_t)42));
    REQUIRE(std::holds_alternative<std::int64_t>(v_int.data));
    REQUIRE(std::get<std::int64_t>(v_int.data) == (int64_t)42);

    StdValue v_dbl = to_std(Json(1.5));
    REQUIRE(std::holds_alternative<double>(v_dbl.data));
    REQUIRE(std::get<double>(v_dbl.data) == 1.5);

    // Borrowed and owned string tags both collapse into std::string.
    static const char kLit[] = "borrowed-view";
    StdValue v_borrowed = to_std(Json(kLit));
    REQUIRE(std::holds_alternative<std::string>(v_borrowed.data));
    REQUIRE(std::get<std::string>(v_borrowed.data) == "borrowed-view");

    StdValue v_owned = to_std(Json::own("owned-copy"));
    REQUIRE(std::holds_alternative<std::string>(v_owned.data));
    REQUIRE(std::get<std::string>(v_owned.data) == "owned-copy");

    StdValue v_arr = to_std(Json(Array::of(Json((int64_t)1), Json(2.5), Json("s"))));
    REQUIRE(std::holds_alternative<StdValue::Array>(v_arr.data));
    const StdValue::Array &a = std::get<StdValue::Array>(v_arr.data);
    REQUIRE(a.size() == 3);
    REQUIRE(std::get<std::int64_t>(a[0].data) == (int64_t)1);
    REQUIRE(std::get<double>(a[1].data) == 2.5);
    REQUIRE(std::get<std::string>(a[2].data) == "s");

    Object obj;
    obj.insert("k", Json((int64_t)1));
    StdValue v_obj = to_std(Json(std::move(obj)));
    REQUIRE(std::holds_alternative<StdValue::Object>(v_obj.data));
    const StdValue::Object &o = std::get<StdValue::Object>(v_obj.data);
    REQUIRE(o.size() == 1);
    REQUIRE(o[0].first == "k");
    REQUIRE(std::get<std::int64_t>(o[0].second.data) == (int64_t)1);
}

TEST_CASE("Interop: to_std deep copy escapes arena") {
    static const char kLong[] = "borrowed-string-longer-than-the-sso-capacity-48";
    StdValue escaped;
    {
        Document doc = parse_copy(
            R"({"s":"borrowed-string-longer-than-the-sso-capacity-48","a":[1,2]})");
        escaped = to_std(doc.root());
    } // doc and its buffer die here

    const StdValue::Object &o = std::get<StdValue::Object>(escaped.data);
    REQUIRE(o.size() == 2);
    REQUIRE(std::get<std::string>(o[0].second.data) == kLong);
    const StdValue::Array &a = std::get<StdValue::Array>(o[1].second.data);
    REQUIRE(a.size() == 2);
    REQUIRE(std::get<std::int64_t>(a[0].data) == (int64_t)1);
    REQUIRE(std::get<std::int64_t>(a[1].data) == (int64_t)2);
}

TEST_CASE("Interop: to_std preserves object order") {
    Document doc = parse_copy(R"({"z":1,"a":2,"m":3})");
    StdValue sv = to_std(doc.root());

    const StdValue::Object &o = std::get<StdValue::Object>(sv.data);
    REQUIRE(o.size() == 3);
    REQUIRE(o[0].first == "z");
    REQUIRE(o[1].first == "a");
    REQUIRE(o[2].first == "m");

    // Order preservation => byte-identical round-trip through dump.
    Json back = from_std(sv);
    REQUIRE(dump(back) == dump(doc.root()));
}

TEST_CASE("Interop: from_std owns into resource") {
    TestCountingResource cr;
    {
        StdValue v{StdValue::Object{
            {"k", StdValue{std::string(48, 'x')}},
            {"a", StdValue{StdValue::Array{StdValue{std::int64_t(1)}}}}}};
        Json j = from_std(v, &cr);
        REQUIRE(cr.outstanding() > 0); // all nodes/strings go through cr
        REQUIRE(j["k"].as_string() == std::string(48, 'x'));
        REQUIRE(j["a"][0] == (int64_t)1);
    }
    REQUIRE(cr.outstanding() == 0); // full return into cr

    // Explicit owned semantics: the std source may die right after the call.
    {
        Json j2;
        {
            StdValue tmp{StdValue::Object{
                {"key", StdValue{std::string(48, 'y')}}}};
            j2 = from_std(tmp, &cr);
        } // tmp dies here
        REQUIRE(j2["key"].as_string() == std::string(48, 'y'));
        REQUIRE(cr.outstanding() > 0);
    }
    REQUIRE(cr.outstanding() == 0);

    // Default resource and null fallback both work (house ctor pattern).
    Json j3 = from_std(StdValue{std::string(48, 'z')});
    REQUIRE(j3.is_string());
    REQUIRE(j3.as_string() == std::string(48, 'z'));

    Json j4 = from_std(StdValue{std::string(48, 'w')}, nullptr);
    REQUIRE(j4.is_string());
    REQUIRE(j4.as_string() == std::string(48, 'w'));
}

TEST_CASE("Interop: round trip equality") {
    Document doc = parse_copy(
        R"({"a":[1,2,{"b":"text"}],"c":true,"d":null,"e":3.5})");
    StdValue sv = to_std(doc.root());

    Json back = from_std(sv);
    REQUIRE(back == doc.root());
    REQUIRE(dump(back) == dump(doc.root()));

    StdValue sv2 = to_std(back);
    REQUIRE(sv2 == sv);

    StdValue scalar{std::string(48, 'q')};
    REQUIRE(to_std(from_std(scalar)) == scalar);
}

TEST_CASE("Interop: copy semantics escape hatch") {
    static_assert(std::is_copy_constructible_v<StdValue>);
    static_assert(std::is_copy_assignable_v<StdValue>);
    static_assert(std::is_copy_constructible_v<StdValue::Array>);
    static_assert(std::is_copy_constructible_v<StdValue::Object>);
    static_assert(!std::is_copy_constructible_v<Json>);
    static_assert(!std::is_copy_assignable_v<Json>);

    StdValue original{StdValue::Array{
        StdValue{std::int64_t(1)}, StdValue{std::string("s")}}};
    StdValue copy = original;

    StdValue::Array &orig_arr = std::get<StdValue::Array>(original.data);
    orig_arr[0] = StdValue{std::int64_t(99)};
    orig_arr[1] = StdValue{std::string("changed")};

    const StdValue::Array &copy_arr = std::get<StdValue::Array>(copy.data);
    REQUIRE(std::get<std::int64_t>(copy_arr[0].data) == (int64_t)1);
    REQUIRE(std::get<std::string>(copy_arr[1].data) == "s");
}

TEST_CASE("Interop: edge shapes") {
    StdValue e_arr = to_std(Json(Array{}));
    REQUIRE(std::get<StdValue::Array>(e_arr.data).empty());

    StdValue e_obj = to_std(Json(Object{}));
    REQUIRE(std::get<StdValue::Object>(e_obj.data).empty());

    StdValue e_str = to_std(Json::own(""));
    REQUIRE(std::get<std::string>(e_str.data).empty());

    StdValue n = to_std(Json(nullptr));
    REQUIRE(std::holds_alternative<std::monostate>(n.data));

    StdValue neg{std::int64_t(-5)};
    REQUIRE(from_std(neg).as_int() == (int64_t)-5);

    const std::int64_t i64_min = std::numeric_limits<std::int64_t>::min();
    StdValue min_v{std::int64_t(i64_min)};
    Json min_j = from_std(min_v);
    REQUIRE(min_j.as_int() == i64_min);
    REQUIRE(std::get<std::int64_t>(to_std(min_j).data) == i64_min);

    StdValue deep{std::int64_t(1)};
    for (int i = 0; i < 64; ++i)
        deep = StdValue{StdValue::Array{std::move(deep)}};
    Json deep_j = from_std(deep);
    REQUIRE(to_std(deep_j) == deep);
}
