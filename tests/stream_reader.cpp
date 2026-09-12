#include <doctest/doctest.h>
#include <ostream>

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <pjh_json/stream.hpp>
#include <pjh_json/writer.hpp>

using namespace pjh::json;

namespace
{
    // doctest has no per-case setup/teardown: a case that mutates the global
    // Config singleton restores it through RAII so the restore still runs when
    // a REQUIRE unwinds the case (json_parser.cpp precedent).
    struct ConfigGuard
    {
        bool m_strict;
        size_t m_arena_block;
        size_t m_max_depth;
        bool m_strip_bom;
        bool m_strict_utf8;

        ConfigGuard()
            : m_strict(Config::instance().strict_duplicate_keys()),
              m_arena_block(Config::instance().arena_block_size()),
              m_max_depth(Config::instance().max_depth()),
              m_strip_bom(Config::instance().strip_bom()),
              m_strict_utf8(Config::instance().strict_utf8())
        {
        }

        ~ConfigGuard()
        {
            Config::instance().set_strict_duplicate_keys(m_strict);
            Config::instance().set_arena_block_size(m_arena_block);
            Config::instance().set_max_depth(m_max_depth);
            Config::instance().set_strip_bom(m_strip_bom);
            Config::instance().set_strict_utf8(m_strict_utf8);
        }
    };

    inline std::string bom()
    {
        return std::string("\xEF\xBB\xBF", 3);
    }
}

TEST_CASE("Stream: jsonl reader round trip") {
    // Mixed kinds, blank line, whitespace-only line, LF and CRLF endings.
    const std::string input =
        "{\"a\":1}\n"
        "[1,2,3]\n"
        "\n"
        "  \r\n"
        "true\n"
        "3.5\r\n";

    // Reference: parse_jsonl over the very same bytes, one element per line.
    auto reference = parse_jsonl(input);
    REQUIRE(reference.root().is_array());
    REQUIRE(reference.root().size() == 4);

    std::istringstream in(input);
    JsonlReader reader(in);

    Document doc;
    size_t idx = 0;
    while (reader.next(doc))
    {
        REQUIRE(idx < reference.root().size());
        REQUIRE(dump(doc.root()) == dump(reference.root()[idx]));
        ++idx;
    }
    REQUIRE(!reader.has_error());
    REQUIRE(idx == 4);

    // Clean end is idempotent.
    REQUIRE(reader.next(doc) == false);
    REQUIRE(!reader.has_error());

    // Throwing shell on the same input, including its nullopt end.
    std::istringstream in2(input);
    JsonlReader shell(in2);
    size_t shell_idx = 0;
    for (;;)
    {
        std::optional<Document> v = shell.next();
        if (!v.has_value())
            break;
        REQUIRE(dump(v->root()) == dump(reference.root()[shell_idx]));
        ++shell_idx;
    }
    REQUIRE(shell_idx == 4);
    REQUIRE(!shell.has_error());
}

TEST_CASE("Stream: jsonl reader empty and eof") {
    {
        // Empty stream: clean end on the very first pull, no error.
        std::istringstream empty("");
        JsonlReader reader(empty);
        Document doc;
        REQUIRE(reader.next(doc) == false);
        REQUIRE(!reader.has_error());
        REQUIRE(reader.next(doc) == false);
        REQUIRE(!reader.has_error());
        REQUIRE(!reader.next().has_value());
    }
    {
        // Blank/whitespace-only trailing lines are skipped, not values.
        std::istringstream blanks("\n \n\t\r\n  \r\n");
        JsonlReader reader(blanks);
        Document doc;
        REQUIRE(reader.next(doc) == false);
        REQUIRE(!reader.has_error());
    }
    {
        // Trailing blank lines after a real value keep the end clean.
        std::istringstream tail("1\n\n \n");
        JsonlReader reader(tail);
        Document doc;
        REQUIRE(reader.next(doc));
        REQUIRE(doc.root().as_int() == (int64_t)1);
        REQUIRE(reader.next(doc) == false);
        REQUIRE(!reader.has_error());
    }
}

TEST_CASE("Stream: jsonl reader error offset") {
    // Line 2 ("[2 x]") fails: the offset is relative to that line, not the
    // whole input (whole-input offset would be 7; line offset is 3).
    const std::string input = "[1]\n[2 x]\n[3]";

    auto ref = parse_jsonl_result(input);
    REQUIRE(ref.is_err());
    ParseError ref_err = std::move(ref).unwrap_err();
    REQUIRE(ref_err.offset() == 3);

    std::istringstream in(input);
    JsonlReader reader(in);
    Document doc;
    REQUIRE(reader.next(doc));
    REQUIRE(dump(doc.root()) == "[1]");
    REQUIRE(reader.next(doc) == false);
    REQUIRE(reader.has_error());
    REQUIRE(reader.error().offset() == 3);
    REQUIRE(std::string(reader.error().what()) == std::string(ref_err.what()));
    REQUIRE(reader.error().category() == Category::Parse);

    // Sticky: repeated pulls stay false until the shell throws.
    REQUIRE(reader.next(doc) == false);

    std::istringstream in2(input);
    JsonlReader shell(in2);
    REQUIRE(shell.next().has_value());
    REQUIRE_THROWS_AS((void)shell.next(), ParseError);
    REQUIRE_THROWS_AS((void)shell.next(), ParseError);
    REQUIRE_THROWS_AS((void)shell.next(), ParseError);
}

TEST_CASE("Stream: jsonl reader config knobs") {
    ConfigGuard guard;

    // max_depth is captured per line by parse_copy: root container = level 1.
    Config::instance().set_max_depth(1);
    {
        const std::string input = "[1]\n[[1]]\n";
        auto ref = parse_jsonl_result(input);
        REQUIRE(ref.is_err());
        ParseError ref_err = std::move(ref).unwrap_err();

        std::istringstream in(input);
        JsonlReader reader(in);
        Document doc;
        REQUIRE(reader.next(doc)); // [1] is exactly depth 1
        REQUIRE(reader.next(doc) == false);
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().offset() == ref_err.offset());
        REQUIRE(std::string(reader.error().what()) == std::string(ref_err.what()));
    }

    // strict_utf8: line-relative offset, parse_jsonl parity.
    Config::instance().set_strict_utf8(true);
    {
        const std::string input = "1\n\"a\xFF\"\n";
        auto ref = parse_jsonl_result(input);
        REQUIRE(ref.is_err());
        ParseError ref_err = std::move(ref).unwrap_err();
        REQUIRE(ref_err.offset() == 2);

        std::istringstream in(input);
        JsonlReader reader(in);
        Document doc;
        REQUIRE(reader.next(doc));
        REQUIRE(doc.root().as_int() == (int64_t)1);
        REQUIRE(reader.next(doc) == false);
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().offset() == 2);
        REQUIRE(std::string(reader.error().what()) == std::string(ref_err.what()));
    }

    // strip_bom: consumed once at the whole-stream start only.
    Config::instance().set_strip_bom(true);
    {
        std::istringstream in(bom() + "1\n2\n");
        JsonlReader reader(in);
        Document doc;
        REQUIRE(reader.next(doc));
        REQUIRE(doc.root().as_int() == (int64_t)1);
        REQUIRE(reader.next(doc));
        REQUIRE(doc.root().as_int() == (int64_t)2);
        REQUIRE(reader.next(doc) == false);
        REQUIRE(!reader.has_error());
    }
    {
        // A BOM on line >= 2 stays a parse error at that line's offset 0,
        // matching parse_jsonl's per-line parsers (strip_bom=false there).
        const std::string input = "1\n" + bom() + "2\n";
        auto ref = parse_jsonl_result(input);
        REQUIRE(ref.is_err());
        ParseError ref_err = std::move(ref).unwrap_err();

        std::istringstream in(input);
        JsonlReader reader(in);
        Document doc;
        REQUIRE(reader.next(doc));
        REQUIRE(reader.next(doc) == false);
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().offset() == ref_err.offset());
        REQUIRE(reader.error().offset() == 0);
        REQUIRE(std::string(reader.error().what()) == std::string(ref_err.what()));
    }
    {
        // A second BOM in line 1 is past the whole-stream start: only the
        // first one is consumed and the rest is a parse error.
        std::istringstream in(bom() + bom() + "1\n");
        JsonlReader reader(in);
        Document doc;
        REQUIRE(reader.next(doc) == false);
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().offset() == 0);
    }

    // strip_bom off: a whole-stream BOM is an ordinary parse error at 0.
    Config::instance().set_strip_bom(false);
    {
        std::istringstream in(bom() + "1\n");
        JsonlReader reader(in);
        Document doc;
        REQUIRE(reader.next(doc) == false);
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().offset() == 0);
    }
}
