#include <doctest/doctest.h>
#include <string>
#include <string_view>

#include <pjh_json/document.hpp>
#include <pjh_json/config.hpp>

using namespace pjh::json;

TEST_CASE("Document: ownership") {
    auto d1 = parse_copy(R"({"a":1,"b":[1,2,3]})");
    auto d2 = parse_copy(R"(["x","y","z"])");
    REQUIRE(d1.root()["a"] == (int64_t)1);
    REQUIRE(d2.root()[0] == "x");

    Document moved = std::move(d1);
    REQUIRE(moved.root()["b"].size() == 3);
}

TEST_CASE("Config: storage policy") {
    auto arena_doc = parse_copy(R"({"k":"v"})", Storage::Arena);
    REQUIRE(arena_doc.root()["k"] == "v");

    auto pooled_doc = parse_copy(R"([1,2])", Storage::Pooled);
    REQUIRE(pooled_doc.root().size() == 2);

    auto sys_doc = parse_copy(R"(true)", Storage::SystemDefault);
    REQUIRE(sys_doc.root() == true);
}

TEST_CASE("Config: default storage") {
    Config::instance().configure(Storage::Arena, 8192);
    REQUIRE(Config::instance().storage() == Storage::Arena);

    auto doc = parse_copy(R"({"x":1})");
    REQUIRE(doc.root()["x"] == (int64_t)1);

    Config::instance().configure(Storage::Pooled, 4096);
    REQUIRE(Config::instance().storage() == Storage::Pooled);
}

TEST_CASE("Config: release") {
    {
        Object o;
        o.insert("k", Json((int64_t)1));
        REQUIRE(o.size() == 1);
    }

    Config::instance().release();
    Config::instance().reset();

    auto doc = parse_copy(R"({"after":"release"})");
    REQUIRE(doc.root()["after"] == "release");
}
