#include <iostream>
#include <cassert>
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

void test_dump_compact()
{
    std::cout << "Dump Compact test started." << std::endl;

    auto doc = parse_copy(R"({"name":"pjh","n":42,"ok":true,"nil":null,"arr":[1,2,3]})");
    auto out = dump(doc.root());
    assert(sv(out) == R"({"name":"pjh","n":42,"ok":true,"nil":null,"arr":[1,2,3]})");

    assert(sv(dump(parse_copy("[]").root())) == "[]");
    assert(sv(dump(parse_copy("{}").root())) == "{}");

    std::cout << "Dump Compact test passed." << std::endl;
}

void test_dump_pretty()
{
    std::cout << "Dump Pretty test started." << std::endl;

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
    assert(sv(out) == expected);

    // tab indent
    auto out2 = dump(parse_copy(R"({"x":1})").root(),
                     DumpOptions{.pretty = true, .indent = 1, .indent_char = '\t'});
    assert(sv(out2) == "{\n\t\"x\": 1\n}");

    // empty containers stay collapsed even in pretty
    assert(sv(dump(parse_copy("[]").root(), DumpOptions{.pretty = true})) == "[]");

    std::cout << "Dump Pretty test passed." << std::endl;
}

void test_dump_escaping()
{
    std::cout << "Dump Escaping test started." << std::endl;

    auto doc = parse_copy(R"("line1\nline2\t\"q\"\\end")");
    auto out = dump(doc.root());
    assert(sv(out) == R"("line1\nline2\t\"q\"\\end")");

    // control char -> \u00XX (0x01); build directly (raw ctrl is invalid JSON input)
    Json ctrl = std::string_view("a\x01" "b", 3);
    auto out2 = dump(ctrl);
    assert(sv(out2) == R"("a\u0001b")");

    // UTF-8 passes through raw (emoji)
    auto d3 = parse_copy("\"\xF0\x9F\x98\x80\"");
    auto out3 = dump(d3.root());
    assert(sv(out3) == "\"\xF0\x9F\x98\x80\"");

    // long clean string exercises SIMD bulk path
    std::string_view lng = R"("this is a very long clean string with no escapes at all here")";
    assert(sv(dump(parse_copy(lng).root())) == lng);

    std::cout << "Dump Escaping test passed." << std::endl;
}

void test_dump_numbers()
{
    std::cout << "Dump Numbers test started." << std::endl;

    assert(sv(dump(parse_copy("42").root())) == "42");
    assert(sv(dump(parse_copy("-12345").root())) == "-12345");

    // float round-trips as float (".0" preserved)
    assert(sv(dump(parse_copy("1.0").root())) == "1.0");
    assert(sv(dump(parse_copy("3.5").root())) == "3.5");

    // NaN/Inf can't occur from parse; build one and expect throw
    Json inf = std::numeric_limits<double>::infinity();
    bool threw = false;
    try
    {
        (void)dump(inf);
    }
    catch (const JsonError &)
    {
        threw = true;
    }
    assert(threw);

    std::cout << "Dump Numbers test passed." << std::endl;
}

void test_prettify()
{
    std::cout << "Prettify test started." << std::endl;

    auto out = prettify(R"({"a":[1,2]})", 2);
    const char *expected =
        "{\n"
        "  \"a\": [\n"
        "    1,\n"
        "    2\n"
        "  ]\n"
        "}";
    assert(sv(out) == expected);

    std::cout << "Prettify test passed." << std::endl;
}

void test_jsonl()
{
    std::cout << "JSONL test started." << std::endl;

    std::string_view input =
        "{\"id\":1,\"msg\":\"hi\"}\n"
        "{\"id\":2,\"msg\":\"line\\ntwo\"}\n"
        "\n" // blank line skipped
        "[1,2,3]\n";

    auto doc = parse_jsonl(input);
    assert(doc.root().is_array());
    assert(doc.root().size() == 3);
    assert(doc.root()[0]["id"] == (int64_t)1);
    assert(doc.root()[1]["msg"] == "line\ntwo");
    assert(doc.root()[2].is_array());
    assert(doc.root()[2].size() == 3);

    // round-trip
    auto out = dump_jsonl(doc.root().as_array());
    const char *expected =
        "{\"id\":1,\"msg\":\"hi\"}\n"
        "{\"id\":2,\"msg\":\"line\\ntwo\"}\n"
        "[1,2,3]\n";
    assert(sv(out) == expected);

    std::cout << "JSONL test passed." << std::endl;
}

void test_dump_ascii()
{
    std::cout << "Dump Ascii test started." << std::endl;

    // BMP codepoint (é = U+00E9) and emoji (U+1F600, needs surrogate pair)
    auto d = parse_copy("\"caf\xC3\xA9 \xF0\x9F\x98\x80\"");
    auto out = dump(d.root(), DumpOptions{.ascii = true});
    assert(sv(out) == R"("caf\u00e9 \ud83d\ude00")");

    // default (ascii=false) passes UTF-8 through
    auto out2 = dump(d.root());
    assert(sv(out2) == "\"caf\xC3\xA9 \xF0\x9F\x98\x80\"");

    // escapes still applied in ascii mode
    auto d3 = parse_copy(R"("a\tb")");
    assert(sv(dump(d3.root(), DumpOptions{.ascii = true})) == R"("a\tb")");

    std::cout << "Dump Ascii test passed." << std::endl;
}

void test_dump_sort_keys()
{
    std::cout << "Dump SortKeys test started." << std::endl;

    auto d = parse_copy(R"({"c":1,"a":2,"b":{"z":9,"y":8}})");
    auto out = dump(d.root(), DumpOptions{.sort_keys = true});
    assert(sv(out) == R"({"a":2,"b":{"y":8,"z":9},"c":1})");

    std::cout << "Dump SortKeys test passed." << std::endl;
}

void test_dump_ostream()
{
    std::cout << "Dump Ostream test started." << std::endl;

    auto d = parse_copy(R"({"a":1})");
    std::ostringstream os;
    dump_to(os, d.root());
    assert(os.str() == R"({"a":1})");

    std::cout << "Dump Ostream test passed." << std::endl;
}

int main()
{
    std::cout << "--- Starting JSON Writer Tests ---" << std::endl;

    test_dump_compact();
    test_dump_pretty();
    test_dump_escaping();
    test_dump_numbers();
    test_dump_ascii();
    test_dump_sort_keys();
    test_dump_ostream();
    test_prettify();
    test_jsonl();

    std::cout << "--- All JSON Writer Tests Passed Successfully! ---" << std::endl;
    return 0;
}
