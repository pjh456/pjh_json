#include <doctest/doctest.h>
#include <string>
#include <string_view>
#include <stdexcept>
#include <cstring>
#include <limits>
#include <sstream>
#include <cstdio>
#include <fstream>
#include <iterator>

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

TEST_CASE("Writer: 19-digit round trip") {
    // int-stored 19-digit values round-trip digit-exact
    REQUIRE(sv(dump(parse_copy("9223372036854775807").root())) == "9223372036854775807");
    REQUIRE(sv(dump(parse_copy("-9223372036854775808").root())) == "-9223372036854775808");

    // double-stored boundary: value-level round trip only (to_chars shortest
    // form is lib-dependent, so no exact-text assertion)
    auto dumped = dump(parse_copy("18446744073709551615").root());
    std::string_view out = sv(dumped);
    // Float-ness preserved: to_chars output carries '.'/'e', or write_double
    // appended ".0" (single binary check; doctest cannot decompose '||')
    REQUIRE(out.find_first_of(".e") != std::string_view::npos);
    auto re = parse_copy(out);
    REQUIRE(re.root().is_float());
    REQUIRE(re.root().as_float() == 18446744073709551616.0);
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

TEST_CASE("Writer: dump_file open failure") {
    // Parent directory missing -> is_open() false -> JsonError at the open check
    CHECK_THROWS_WITH(
        (void)dump_file("pjh_no_such_dir_xyz/out.json", Json(true)),
        "Failed to open file for writing: pjh_no_such_dir_xyz/out.json");
}

TEST_CASE("Writer: dump_file round trip") {
    // Success path: the close() state check must not misfire (R2)
    const std::string f = "pjh_dump_roundtrip.json";
    auto d = parse_copy(R"({"a":1,"b":[true,null,"x"]})");
    dump_file(f, d.root());
    auto back = parse_file(f);
    REQUIRE(dump(back.root()) == dump(d.root()));
    std::remove(f.c_str());
}

TEST_CASE("Writer: dump_jsonl_file round trip") {
    // dump_jsonl_file inherits write_file -> close check pinned on success path
    const std::string f = "pjh_dump_roundtrip.jsonl";
    auto arr = Array::of(Json(1), Json("x"), Json(true));
    dump_jsonl_file(f, arr);
    std::ifstream in(f, std::ios::binary);
    REQUIRE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(content == "1\n\"x\"\ntrue\n");
    auto back = parse_jsonl(content);
    REQUIRE(back.root().size() == 3);
    std::remove(f.c_str());
}

TEST_CASE("Writer: dump ostream failure") {
    // Permanently failing streambuf (empty put area + overflow->eof) so
    // os.write sets failbit and the stream-state check throws
    struct FailBuf : std::streambuf
    {
        FailBuf() { setp(nullptr, nullptr); }
        int overflow(int ch) override { (void)ch; return traits_type::eof(); }
    };
    FailBuf buf;
    std::ostream os(&buf);
    auto d = parse_copy(R"({"a":1})");
    CHECK_THROWS_WITH((void)dump_to(os, d.root()), "Failed to write to stream");
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

    // Empty containers do not bypass the limit — parse while depth is
    // unlimited, so only dump() can throw; the dump message pins the
    // dump-side check (a parse-side throw carries the parse message)
    auto doc2 = parse_copy("{\"a\":{}}");
    Config::instance().set_max_depth(1);
    REQUIRE_THROWS_AS((void)dump(doc2.root()), JsonError);
    CHECK_THROWS_WITH((void)dump(doc2.root()),
                      "Maximum nesting depth exceeded during dump");

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

TEST_CASE("Writer: result entry") {
#ifndef __FAST_MATH__
    // non-finite double (write_double): writer channel E = base JsonError
    auto bad = Json(std::numeric_limits<double>::quiet_NaN());
    auto r = dump_result(bad);
    REQUIRE(r.is_err());
    JsonError e = r.unwrap_err();
    REQUIRE(e.category() == Category::Json);
    REQUIRE(dynamic_cast<const JsonError *>(&e) != nullptr);
#endif
    // Success path + payload content
    auto d = parse_copy(R"({"a":1})");
    auto ok = dump_result(d.root());
    REQUIRE(ok.is_ok());
    std::pmr::string s = std::move(ok).unwrap();
    REQUIRE(s == R"({"a":1})");
    // File shell (parent dir missing -> is_open false, cross-platform
    // deterministic)
    auto rf = dump_file_result("pjh_no_such_dir_xyz/out.json", d.root());
    REQUIRE(rf.is_err());
    JsonError ef = rf.unwrap_err();
    REQUIRE(ef.category() == Category::Json);
}

TEST_CASE("Writer: document result entry") {
    Config::instance().set_max_depth(0); // defensive: clear prior case state

    // Success: the Document overload serializes the root value (payload pin)
    auto d = parse_copy("[[]]");
    auto ok = dump_result(d);
    REQUIRE(ok.is_ok());
    auto s = std::move(ok).unwrap();
    REQUIRE(sv(s) == "[[]]");

    // Failing path: a max_depth violation is a base JsonError (not a
    // ParseError) with the context-free writer message
    Config::instance().set_max_depth(1);
    auto er = dump_result(d);
    REQUIRE(er.is_err());
    JsonError e = er.unwrap_err();
    REQUIRE(e.category() == Category::Json);
    REQUIRE(dynamic_cast<const JsonError *>(&e) != nullptr);
    REQUIRE(dynamic_cast<const ParseError *>(&e) == nullptr);
    REQUIRE(std::string(e.what()) ==
            "Maximum nesting depth exceeded during dump");

    Config::instance().set_max_depth(0); // restore
}

TEST_CASE("Writer: jsonl result entry") {
    // Success: one compact line per element (mirrors "Writer: JSONL")
    std::string_view input =
        "{\"id\":1,\"msg\":\"hi\"}\n"
        "{\"id\":2,\"msg\":\"line\\ntwo\"}\n"
        "\n"
        "[1,2,3]\n";
    auto doc = parse_jsonl(input);
    auto r = dump_jsonl_result(doc.root().as_array());
    REQUIRE(r.is_ok());
    const char *expected =
        "{\"id\":1,\"msg\":\"hi\"}\n"
        "{\"id\":2,\"msg\":\"line\\ntwo\"}\n"
        "[1,2,3]\n";
    auto s = std::move(r).unwrap();
    REQUIRE(sv(s) == expected);

#ifndef __FAST_MATH__
    // Non-finite element: writer channel E = base JsonError
    auto bad = Array::of(Json(std::numeric_limits<double>::quiet_NaN()));
    auto er = dump_jsonl_result(bad);
    REQUIRE(er.is_err());
    JsonError e = er.unwrap_err();
    REQUIRE(e.category() == Category::Json);
    REQUIRE(dynamic_cast<const JsonError *>(&e) != nullptr);
#endif
}

TEST_CASE("Writer: jsonl file result entry") {
    // Success: the payload IS the content written to the file — read the
    // file back and compare against the channel string
    const std::string f = "pjh_result_jsonl.jsonl";
    auto arr = Array::of(Json(1), Json("x"), Json(true));
    auto r = dump_jsonl_file_result(f, arr);
    REQUIRE(r.is_ok());
    auto s = std::move(r).unwrap();
    REQUIRE(sv(s) == "1\n\"x\"\ntrue\n");
    std::ifstream in(f, std::ios::binary);
    REQUIRE(in.is_open());
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    REQUIRE(content == "1\n\"x\"\ntrue\n");
    std::remove(f.c_str());

    // Failing path: parent dir missing -> open check throws the base JsonError
    auto er = dump_jsonl_file_result("pjh_no_such_dir_xyz/out.jsonl", arr);
    REQUIRE(er.is_err());
    JsonError e = er.unwrap_err();
    REQUIRE(e.category() == Category::Json);
    REQUIRE(dynamic_cast<const JsonError *>(&e) != nullptr);
}
