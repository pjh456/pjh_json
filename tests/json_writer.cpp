#include <doctest/doctest.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <cstring>
#include <limits>
#include <sstream>

#include <pjh_json/document.hpp>
#include <pjh_json/writer.hpp>

using namespace pjh::json;

static std::string_view sv(const std::pmr::string &s)
{
    return std::string_view(s.data(), s.size());
}

TEST_CASE("Writer: dump compact") {
    auto doc = parse_copy(R"({"name":"pjh","n":42,"ok":true,"nil":null,"arr":[1,2,3]})");
    auto out = dump(doc.root());
    REQUIRE(sv(out) == R"({"name":"pjh","n":42,"ok":true,"nil":null,"arr":[1,2,3]})");

    REQUIRE(sv(dump(parse_copy("[]").root())) == "[]");
    REQUIRE(sv(dump(parse_copy("{}").root())) == "{}");
}

TEST_CASE("Writer: dump pretty") {
    auto doc = parse_copy(R"({"a":1,"b":[2,3]})");
    auto out = dump(doc.root(), DumpOptions{.pretty = true, .indent = 2});
    const char *expected =
        "{\n"
        "  \"a\": 1,\n"
        "  \"b\": [\n"
        "    2,\n"
        "    3\n"
        "  ]\n"
        "}";
    REQUIRE(sv(out) == expected);

    auto out2 = dump(parse_copy(R"({"x":1})").root(),
                     DumpOptions{.pretty = true, .indent = 1, .indent_char = '\t'});
    REQUIRE(sv(out2) == "{\n\t\"x\": 1\n}");

    REQUIRE(sv(dump(parse_copy("[]").root(), DumpOptions{.pretty = true})) == "[]");
}

TEST_CASE("Writer: dump escaping") {
    auto doc = parse_copy(R"("line1\nline2\t\"q\"\\end")");
    auto out = dump(doc.root());
    REQUIRE(sv(out) == R"("line1\nline2\t\"q\"\\end")");

    Json ctrl = std::string_view("a\x01"
                                 "b",
                                 3);
    auto out2 = dump(ctrl);
    REQUIRE(sv(out2) == R"("a\u0001b")");

    auto d3 = parse_copy("\"\xF0\x9F\x98\x80\"");
    auto out3 = dump(d3.root());
    REQUIRE(sv(out3) == "\"\xF0\x9F\x98\x80\"");

    std::string_view lng = R"("this is a very long clean string with no escapes at all here")";
    REQUIRE(sv(dump(parse_copy(lng).root())) == lng);
}

TEST_CASE("Writer: dump numbers") {
    REQUIRE(sv(dump(parse_copy("42").root())) == "42");
    REQUIRE(sv(dump(parse_copy("-12345").root())) == "-12345");

    REQUIRE(sv(dump(parse_copy("1.0").root())) == "1.0");
    REQUIRE(sv(dump(parse_copy("3.5").root())) == "3.5");

    Json inf = std::numeric_limits<double>::infinity();
    bool threw = false;
    try {
        (void)dump(inf);
    } catch (const JsonError &) {
        threw = true;
    }
#ifndef __FAST_MATH__
    REQUIRE(threw);
#endif
}

TEST_CASE("Writer: prettify") {
    auto out = prettify(R"({"a":[1,2]})", {.pretty = true, .indent = 2});
    const char *expected =
        "{\n"
        "  \"a\": [\n"
        "    1,\n"
        "    2\n"
        "  ]\n"
        "}";
    REQUIRE(sv(out) == expected);
}

TEST_CASE("Writer: JSONL") {
    std::string_view input =
        "{\"id\":1,\"msg\":\"hi\"}\n"
        "{\"id\":2,\"msg\":\"line\\ntwo\"}\n"
        "\n"
        "[1,2,3]\n";

    auto doc = parse_jsonl(input);
    REQUIRE(doc.root().is_array());
    REQUIRE(doc.root().size() == 3);
    REQUIRE(doc.root()[0]["id"] == (int64_t)1);
    REQUIRE(doc.root()[1]["msg"] == "line\ntwo");
    REQUIRE(doc.root()[2].is_array());
    REQUIRE(doc.root()[2].size() == 3);

    auto out = dump_jsonl(doc.root().as_array());
    const char *expected =
        "{\"id\":1,\"msg\":\"hi\"}\n"
        "{\"id\":2,\"msg\":\"line\\ntwo\"}\n"
        "[1,2,3]\n";
    REQUIRE(sv(out) == expected);
}

TEST_CASE("Writer: dump ascii") {
    auto d = parse_copy("\"caf\xC3\xA9 \xF0\x9F\x98\x80\"");
    auto out = dump(d.root(), DumpOptions{.ascii = true});
    REQUIRE(sv(out) == R"("caf\u00e9 \ud83d\ude00")");

    auto out2 = dump(d.root());
    REQUIRE(sv(out2) == "\"caf\xC3\xA9 \xF0\x9F\x98\x80\"");

    auto d3 = parse_copy(R"("a\tb")");
    REQUIRE(sv(dump(d3.root(), DumpOptions{.ascii = true})) == R"("a\tb")");
}

TEST_CASE("Writer: dump sort keys") {
    auto d = parse_copy(R"({"c":1,"a":2,"b":{"z":9,"y":8}})");
    auto out = dump(d.root(), DumpOptions{.sort_keys = true});
    REQUIRE(sv(out) == R"({"a":2,"b":{"y":8,"z":9},"c":1})");
}

TEST_CASE("Writer: dump ostream") {
    auto d = parse_copy(R"({"a":1})");
    std::ostringstream os;
    dump_to(os, d.root());
    REQUIRE(os.str() == R"({"a":1})");
}

TEST_CASE("Writer: max depth") {
    Config::instance().set_max_depth(0); // defensive: clear prior case state

    auto deep = [](size_t n, char o, char c)
    {
        return std::string(n, o) + std::string(n, c);
    };

    // Default: unlimited (regression pin, round-trip)
    auto doc100 = parse_copy(deep(100, '[', ']'));
    REQUIRE(sv(dump(doc100.root())) == deep(100, '[', ']'));

    // Exactly N passes, N+1 throws
    auto doc60 = parse_copy(deep(60, '[', ']'));
    Config::instance().set_max_depth(60);
    REQUIRE(sv(dump(doc60.root())) == deep(60, '[', ']'));
    Config::instance().set_max_depth(59);
    REQUIRE_THROWS_AS((void)dump(doc60.root()), JsonError);
    CHECK_THROWS_WITH((void)dump(doc60.root()),
                      "Maximum nesting depth exceeded during dump");

    // Empty containers do not bypass the limit
    Config::instance().set_max_depth(1);
    REQUIRE_THROWS_AS((void)dump(parse_copy("{\"a\":{}}").root()), JsonError);

    // The failure is a JsonError, not a ParseError
    bool caught = false;
    try {
        (void)dump(doc60.root());
    } catch (const JsonError &e) {
        caught = true;
        CHECK(dynamic_cast<const ParseError *>(&e) == nullptr);
    }
    REQUIRE(caught);

    Config::instance().set_max_depth(0); // restore
}
