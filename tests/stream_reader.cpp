#include <doctest/doctest.h>
#include <ostream>

#include <cstdint>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
        bool m_json5;

        ConfigGuard()
            : m_strict(Config::instance().strict_duplicate_keys()),
              m_arena_block(Config::instance().arena_block_size()),
              m_max_depth(Config::instance().max_depth()),
              m_strip_bom(Config::instance().strip_bom()),
              m_strict_utf8(Config::instance().strict_utf8()),
              m_json5(Config::instance().json5())
        {
        }

        ~ConfigGuard()
        {
            Config::instance().set_strict_duplicate_keys(m_strict);
            Config::instance().set_arena_block_size(m_arena_block);
            Config::instance().set_max_depth(m_max_depth);
            Config::instance().set_strip_bom(m_strip_bom);
            Config::instance().set_strict_utf8(m_strict_utf8);
            Config::instance().set_json5(m_json5);
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

namespace
{
    // An event with owned text: reading past the next() call must not be
    // required (the borrowed view is copied immediately).
    struct OwnedEvent
    {
        EventType type = EventType::Null;
        std::string text;
        bool boolean = false;
        int64_t integer = 0;
        double number = 0.0;
    };

    std::vector<OwnedEvent> collect_events(StreamReader &reader)
    {
        std::vector<OwnedEvent> events;
        JsonEvent ev;
        while (reader.next(ev))
        {
            OwnedEvent owned;
            owned.type = ev.type;
            owned.text = std::string(ev.text);
            owned.boolean = ev.boolean;
            owned.integer = ev.integer;
            owned.number = ev.number;
            events.push_back(std::move(owned));
        }
        return events;
    }

    bool same_events(const std::vector<OwnedEvent> &a, const std::vector<OwnedEvent> &b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); ++i)
        {
            if (a[i].type != b[i].type || a[i].text != b[i].text || a[i].boolean != b[i].boolean ||
                a[i].integer != b[i].integer || a[i].number != b[i].number)
                return false;
        }
        return true;
    }

    // Rebuild a Json tree from the flat event stream (the plan's core
    // differential: dump(rebuilt) == dump(parse_copy(input))).
    Json rebuild_value(const std::vector<OwnedEvent> &events, size_t &i, std::pmr::memory_resource *res)
    {
        const OwnedEvent &ev = events[i++];
        switch (ev.type)
        {
        case EventType::Null:
            return Json(nullptr);
        case EventType::Boolean:
            return Json(ev.boolean);
        case EventType::Integer:
            return Json(ev.integer);
        case EventType::Double:
            return Json(ev.number);
        case EventType::String:
            return Json::own(ev.text, res);
        case EventType::BeginArray:
        {
            Array arr(res);
            while (events[i].type != EventType::EndArray)
                arr.push_back(rebuild_value(events, i, res));
            ++i;
            return Json(std::move(arr));
        }
        case EventType::BeginObject:
        {
            Object obj(res);
            while (events[i].type != EventType::EndObject)
            {
                std::string key = events[i++].text;
                obj.insert(key, rebuild_value(events, i, res), res);
            }
            ++i;
            return Json(std::move(obj));
        }
        case EventType::MapKey:
        case EventType::EndObject:
        case EventType::EndArray:
            break;
        }
        return Json(nullptr);
    }

    // Pull every event until clean end or failure, then assert the failure.
    void expect_error(const std::string &input, ErrorCode code, size_t offset)
    {
        std::istringstream in(input);
        StreamReader reader(in, 3); // tiny window also exercises refill paths
        JsonEvent ev;
        while (reader.next(ev))
        {
        }
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().code == code);
        REQUIRE(reader.error().offset() == offset);

        // Both entry points must reject the same input (classification parity
        // is sampled here, not pinned byte-for-byte).
        REQUIRE(parse_copy_result(input).is_err());
    }

    // Snapshot a borrowed event into the owned test form.
    OwnedEvent own_of(const JsonEvent &ev)
    {
        OwnedEvent owned;
        owned.type = ev.type;
        owned.text = std::string(ev.text);
        owned.boolean = ev.boolean;
        owned.integer = ev.integer;
        owned.number = ev.number;
        return owned;
    }

    // Drain an items(prefix) cursor, asserting the underlying reader is clean.
    std::vector<OwnedEvent> collect_item_events(const std::string &input, std::string_view prefix, size_t chunk = 4)
    {
        std::istringstream in(input);
        StreamReader reader(in, chunk);
        StreamItems items = reader.items(prefix);
        std::vector<OwnedEvent> events;
        JsonEvent ev;
        while (items.next(ev))
            events.push_back(own_of(ev));
        REQUIRE(!items.has_error());
        return events;
    }

    OwnedEvent ev_type(EventType type)
    {
        OwnedEvent ev;
        ev.type = type;
        return ev;
    }

    OwnedEvent ev_str(EventType type, std::string text)
    {
        OwnedEvent ev;
        ev.type = type;
        ev.text = std::move(text);
        return ev;
    }

    OwnedEvent ev_int(int64_t value)
    {
        OwnedEvent ev;
        ev.type = EventType::Integer;
        ev.integer = value;
        return ev;
    }
}

TEST_CASE("Stream: event sequence")
{
    const std::string input = "{\"a\":[1,true],\"b\":{\"c\":null},\"d\":\"x\"}";
    std::istringstream in(input);
    StreamReader reader(in);
    auto events = collect_events(reader);
    REQUIRE(!reader.has_error());

    const std::vector<EventType> expected = {
        EventType::BeginObject, EventType::MapKey, EventType::BeginArray,  EventType::Integer,   EventType::Boolean,
        EventType::EndArray,    EventType::MapKey, EventType::BeginObject, EventType::MapKey,    EventType::Null,
        EventType::EndObject,   EventType::MapKey, EventType::String,      EventType::EndObject,
    };
    REQUIRE(events.size() == expected.size());
    for (size_t i = 0; i < expected.size(); ++i)
        REQUIRE(events[i].type == expected[i]);

    REQUIRE(events[1].text == "a");
    REQUIRE(events[3].integer == (int64_t)1);
    REQUIRE(events[4].boolean == true);
    REQUIRE(events[6].text == "b");
    REQUIRE(events[8].text == "c");
    REQUIRE(events[12].text == "x");
}

TEST_CASE("Stream: single root values")
{
    const char *inputs[] = {"null", "true", "false", "0", "-17", "3.5", "\"hi\"", "{}", "[]"};
    for (const char *input : inputs)
    {
        std::istringstream in(input);
        StreamReader reader(in);
        auto events = collect_events(reader);
        REQUIRE(!reader.has_error());
        REQUIRE(!events.empty());
        REQUIRE(events.front().type != EventType::EndObject);
        REQUIRE(events.front().type != EventType::EndArray);
    }

    {
        std::istringstream in("null");
        StreamReader reader(in);
        JsonEvent ev;
        REQUIRE(reader.next(ev));
        REQUIRE(ev.type == EventType::Null);
        REQUIRE(reader.next(ev) == false);
        REQUIRE(!reader.has_error());
    }
    {
        std::istringstream in("-17");
        StreamReader reader(in);
        JsonEvent ev;
        REQUIRE(reader.next(ev));
        REQUIRE(ev.type == EventType::Integer);
        REQUIRE(ev.integer == (int64_t)-17);
    }
    {
        std::istringstream in("3.5e2");
        StreamReader reader(in);
        JsonEvent ev;
        REQUIRE(reader.next(ev));
        REQUIRE(ev.type == EventType::Double);
        REQUIRE(ev.number == 350.0);
    }
}

TEST_CASE("Stream: events rebuild dom")
{
    const char *inputs[] = {
        "null",
        "true",
        "-123456789",
        "18446744073709551615",
        "1.5e3",
        "\"a\\nb\\u00e9\\ud83d\\ude00\"",
        "{}",
        "[]",
        "{\"a\":1,\"b\":[true,false,null,3.5,-7,1e3],\"c\":{\"d\":\"x\\ny\"}}",
        "[{\"k\":\"v\"},[1,[2,[3]]]]",
        "[[],{},[{}]]",
    };

    std::pmr::memory_resource *res = Config::instance().resource();
    for (const char *input : inputs)
    {
        std::istringstream in(input);
        StreamReader reader(in, 2);
        auto events = collect_events(reader);
        REQUIRE(!reader.has_error());

        size_t cursor = 0;
        Json rebuilt = rebuild_value(events, cursor, res);
        REQUIRE(cursor == events.size());

        auto reference = parse_copy(input);
        REQUIRE(dump(rebuilt) == dump(reference.root()));
    }
}

TEST_CASE("Stream: chunk boundary")
{
    const std::string long_string(1000, 'q');
    const std::string input = "{\"key1\":\"abcdefghijklmnopqrstuvwxyz0123456789\","
                              "\"key2\":1234567890123456789012345,"
                              "\"key3\":[true,false,null,{\"deep\":\"end\"}],"
                              "\"key4\":\"\\u00e9\\ud83d\\ude00\","
                              "\"key5\":\"" +
                              long_string + "\"}";

    std::istringstream baseline_in(input);
    StreamReader baseline(baseline_in, 64 * 1024);
    auto expected = collect_events(baseline);
    REQUIRE(!baseline.has_error());

    for (size_t chunk : {size_t(1), size_t(2), size_t(3), size_t(7)})
    {
        std::istringstream in(input);
        StreamReader reader(in, chunk);
        auto events = collect_events(reader);
        REQUIRE(!reader.has_error());
        REQUIRE(same_events(events, expected));
    }

    // The decoded long string keeps every byte regardless of window splits.
    std::istringstream long_in("[\"" + long_string + "\"]");
    StreamReader long_reader(long_in, 4);
    auto long_events = collect_events(long_reader);
    REQUIRE(!long_reader.has_error());
    REQUIRE(long_events.size() == 3);
    REQUIRE(long_events[1].type == EventType::String);
    REQUIRE(long_events[1].text == long_string);
}

TEST_CASE("Stream: large bounded")
{
    // An array much larger than the window: memory stays bounded by the
    // window while the element count is exact, proving no whole-input
    // buffering assumption.
    std::string input = "[";
    const size_t count = 2000;
    for (size_t i = 0; i < count; ++i)
    {
        if (i != 0)
            input += ',';
        input += std::to_string(i);
    }
    input += ']';

    std::istringstream in(input);
    StreamReader reader(in, 16);
    JsonEvent ev;
    size_t integers = 0;
    while (reader.next(ev))
    {
        if (ev.type == EventType::Integer)
            ++integers;
    }
    REQUIRE(!reader.has_error());
    REQUIRE(integers == count);
}

TEST_CASE("Stream: depth limit")
{
    ConfigGuard guard;

    Config::instance().set_max_depth(1);
    {
        // A root container counts as level 1; a nested container is rejected
        // at the second opening bracket.
        std::istringstream in("[[1]]");
        StreamReader reader(in);
        JsonEvent ev;
        REQUIRE(reader.next(ev));
        REQUIRE(ev.type == EventType::BeginArray);
        REQUIRE(reader.next(ev) == false);
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().code == ErrorCode::MaxDepthExceeded);
        REQUIRE(reader.error().offset() == 1);
    }
    {
        std::istringstream in("[1]");
        StreamReader reader(in);
        auto events = collect_events(reader);
        REQUIRE(!reader.has_error());
        REQUIRE(events.size() == 3);
    }

    Config::instance().set_max_depth(2);
    {
        std::istringstream in("[[1]]");
        StreamReader reader(in);
        auto events = collect_events(reader);
        REQUIRE(!reader.has_error());
        REQUIRE(events.size() == 5);
    }
}

TEST_CASE("Stream: errors")
{
    // Structure.
    expect_error("[1 2]", ErrorCode::ExpectedCommaOrBracket, 3);
    expect_error("{\"a\" 1}", ErrorCode::ExpectedColon, 5);
    expect_error("[1,]", ErrorCode::UnexpectedCharacter, 3);
    expect_error("{\"a\":}", ErrorCode::UnexpectedCharacter, 5);
    expect_error("[", ErrorCode::UnexpectedCharacter, 1);
    expect_error("{\"a\":1,}", ErrorCode::ExpectedStringKey, 7);

    // Numbers.
    expect_error("[01]", ErrorCode::NumberLeadingZero, 3);
    expect_error("[1.]", ErrorCode::NumberNoFracDigits, 3);
    expect_error("[1e]", ErrorCode::NumberNoExpDigits, 3);

    // Strings.
    expect_error("\"abc", ErrorCode::UnterminatedString, 4);
    expect_error("\"a\nb\"", ErrorCode::UnescapedControl, 2);
    expect_error("\"\\q\"", ErrorCode::InvalidEscapeChar, 2);

    // Literals and trailing content.
    expect_error("truex", ErrorCode::InvalidLiteralTrailing, 4);
    expect_error("[truex]", ErrorCode::InvalidLiteralTrailing, 5);
    expect_error("1 2", ErrorCode::ExtraCharactersAfterValue, 2);
}

TEST_CASE("Stream: eof mid token")
{
    expect_error("", ErrorCode::UnexpectedEndOfInput, 0);
    expect_error("{", ErrorCode::ExpectedStringKey, 1);
    expect_error("[1", ErrorCode::ExpectedCommaOrBracket, 2);
    expect_error("{\"a\":1", ErrorCode::UnexpectedEndOfObject, 6);
    expect_error("{\"a\":", ErrorCode::UnexpectedCharacter, 5);
    expect_error("[1,", ErrorCode::UnexpectedCharacter, 3);
    // A truncated literal is a mismatch for Parser::parse_literal (its
    // fixed-width read sees NUL padding), so the DOM parser reports
    // InvalidLiteral at the literal start, never UnexpectedEndOfInput.
    expect_error("tru", ErrorCode::InvalidLiteral, 0);
    expect_error("fals", ErrorCode::InvalidLiteral, 0);
    expect_error("-", ErrorCode::NumberNoIntDigits, 1);
    expect_error("1e+", ErrorCode::NumberNoExpDigits, 3);
}

TEST_CASE("Stream: borrowed view lifetime")
{
    // The event's text is copied before the next pull; that is the whole
    // contract (the buffer is reused by the following event).
    std::istringstream in("[\"alpha\",\"beta\",\"gamma\"]");
    StreamReader reader(in, 1);
    JsonEvent ev;
    REQUIRE(reader.next(ev));
    REQUIRE(ev.type == EventType::BeginArray);

    std::string first;
    REQUIRE(reader.next(ev));
    REQUIRE(ev.type == EventType::String);
    first = std::string(ev.text);
    REQUIRE(first == "alpha");

    REQUIRE(reader.next(ev));
    REQUIRE(ev.type == EventType::String);
    REQUIRE(std::string(ev.text) == "beta");

    REQUIRE(reader.next(ev));
    REQUIRE(ev.type == EventType::String);
    REQUIRE(std::string(ev.text) == "gamma");

    // A decoded escape is also copied out before the buffer is reused.
    REQUIRE(reader.next(ev));
    REQUIRE(ev.type == EventType::EndArray);
    REQUIRE(reader.next(ev) == false);
    REQUIRE(!reader.has_error());

    // Throwing shell / result shell.
    std::istringstream shell_in("\"esc\\n\"");
    StreamReader shell(shell_in, 2);
    std::optional<JsonEvent> maybe = shell.next();
    REQUIRE(maybe.has_value());
    REQUIRE(maybe->type == EventType::String);
    REQUIRE(std::string(maybe->text) == "esc\n");
    REQUIRE(!shell.next().has_value());
    REQUIRE(!shell.has_error());

    std::istringstream result_in("42");
    StreamReader result_reader(result_in);
    auto ok = result_reader.next_result();
    REQUIRE(ok.is_ok());
    REQUIRE(ok.unwrap().has_value());
    REQUIRE(ok.unwrap()->type == EventType::Integer);
    REQUIRE(ok.unwrap()->integer == (int64_t)42);
    auto end = result_reader.next_result();
    REQUIRE(end.is_ok());
    REQUIRE(!end.unwrap().has_value());

    std::istringstream bad_in("[1 2]");
    StreamReader bad_reader(bad_in);
    JsonEvent sink;
    while (bad_reader.next(sink))
    {
    }
    auto err = bad_reader.next_result();
    REQUIRE(err.is_err());
    REQUIRE(err.unwrap_err().offset() == 3);
}

TEST_CASE("Stream: multi value jsonl sequence")
{
    // Whitespace/newline-separated values: the JSONL generalization. Blank
    // and whitespace-only lines are ordinary whitespace, CRLF too.
    const std::string input = "{\"a\":1}\n"
                              "[1,2,3]\n"
                              "\n"
                              "  \r\n"
                              "true\n"
                              "3.5\r\n"
                              "\"hi\"\n"
                              "null\n";
    const char *expected[] = {"{\"a\":1}", "[1,2,3]", "true", "3.5", "\"hi\"", "null"};

    std::istringstream in(input);
    StreamReader reader(in, StreamMode::MultiValue, 2); // tiny window
    auto events = collect_events(reader);
    REQUIRE(!reader.has_error());

    std::pmr::memory_resource *res = Config::instance().resource();
    size_t cursor = 0;
    for (const char *want : expected)
    {
        CAPTURE(want);
        Json rebuilt = rebuild_value(events, cursor, res);
        auto reference = parse_copy(want);
        REQUIRE(dump(rebuilt) == dump(reference.root()));
    }
    REQUIRE(cursor == events.size());

    // Clean end is idempotent.
    JsonEvent ev;
    REQUIRE(reader.next(ev) == false);
    REQUIRE(!reader.has_error());
}

TEST_CASE("Stream: multi value empty stream")
{
    // Zero values is a clean end in MultiValue mode (unlike SingleRoot, where
    // empty input is UnexpectedEndOfInput).
    for (const char *input : {"", " \t\r\n", "\n\n"})
    {
        CAPTURE(input);
        std::istringstream in(input);
        StreamReader reader(in, StreamMode::MultiValue);
        JsonEvent ev;
        REQUIRE(reader.next(ev) == false);
        REQUIRE(!reader.has_error());
        REQUIRE(reader.next(ev) == false);
    }
    {
        std::istringstream in("");
        StreamReader single(in); // default SingleRoot
        JsonEvent ev;
        REQUIRE(single.next(ev) == false);
        REQUIRE(single.has_error());
        REQUIRE(single.error().code == ErrorCode::UnexpectedEndOfInput);
    }
}

TEST_CASE("Stream: multi value separators and result shell")
{
    // Whitespace separates top-level values.
    std::istringstream in("1 true [2] {}");
    StreamReader reader(in, StreamMode::MultiValue);
    auto events = collect_events(reader);
    REQUIRE(!reader.has_error());

    std::pmr::memory_resource *res = Config::instance().resource();
    const char *expected[] = {"1", "true", "[2]", "{}"};
    size_t cursor = 0;
    for (const char *want : expected)
    {
        CAPTURE(want);
        REQUIRE(dump(rebuild_value(events, cursor, res)) == want);
    }
    REQUIRE(cursor == events.size());

    // Values must be whitespace-separated: no separator is the same
    // ExtraCharactersAfterValue SingleRoot reports, in either mode.
    std::istringstream single_in("1true");
    StreamReader single(single_in);
    JsonEvent ev;
    while (single.next(ev))
    {
    }
    REQUIRE(single.has_error());
    REQUIRE(single.error().code == ErrorCode::ExtraCharactersAfterValue);
    REQUIRE(single.error().offset() == 1);

    std::istringstream multi_in("1true");
    StreamReader multi(multi_in, StreamMode::MultiValue);
    REQUIRE(multi.next(ev));
    REQUIRE(ev.integer == (int64_t)1);
    REQUIRE(multi.next(ev) == false);
    REQUIRE(multi.has_error());
    REQUIRE(multi.error().code == ErrorCode::ExtraCharactersAfterValue);
    REQUIRE(multi.error().offset() == 1);

    // Result shell in MultiValue mode: Ok per event, final Ok(nullopt).
    std::istringstream result_in("1 2");
    StreamReader rr(result_in, StreamMode::MultiValue);
    auto r1 = rr.next_result();
    REQUIRE(r1.is_ok());
    REQUIRE(r1.unwrap().has_value());
    REQUIRE(r1.unwrap()->type == EventType::Integer);
    REQUIRE(r1.unwrap()->integer == (int64_t)1);
    auto r2 = rr.next_result();
    REQUIRE(r2.is_ok());
    REQUIRE(r2.unwrap().has_value());
    REQUIRE(r2.unwrap()->integer == (int64_t)2);
    auto r3 = rr.next_result();
    REQUIRE(r3.is_ok());
    REQUIRE(!r3.unwrap().has_value());
}

TEST_CASE("Stream: multi value absolute error offset")
{
    // MultiValue offsets stay absolute from the stream start (unlike
    // JsonlReader / parse_jsonl, whose offsets are line-relative).
    const std::string input = "1\n[2 x]\n";
    std::istringstream in(input);
    StreamReader reader(in, StreamMode::MultiValue, 3);
    JsonEvent ev;
    REQUIRE(reader.next(ev));
    REQUIRE(ev.type == EventType::Integer);
    REQUIRE(ev.integer == (int64_t)1);
    while (reader.next(ev))
    {
    }
    REQUIRE(reader.has_error());
    REQUIRE(reader.error().code == ErrorCode::ExpectedCommaOrBracket);
    REQUIRE(reader.error().offset() == 5); // index of 'x', absolute
}

TEST_CASE("Stream: strip bom")
{
    ConfigGuard guard;
    Config::instance().set_strip_bom(true);
    Config::instance().set_strict_utf8(false);

    // SingleRoot: a byte-0 BOM is consumed.
    {
        std::istringstream in(bom() + "{}");
        StreamReader reader(in);
        auto events = collect_events(reader);
        REQUIRE(!reader.has_error());
        REQUIRE(events.size() == 2);
        REQUIRE(events[0].type == EventType::BeginObject);
    }
    // MultiValue: the BOM is consumed once, at the stream start.
    {
        std::istringstream in(bom() + "1\n2\n");
        StreamReader reader(in, StreamMode::MultiValue);
        auto events = collect_events(reader);
        REQUIRE(!reader.has_error());
        REQUIRE(events.size() == 2);
        REQUIRE(events[0].integer == (int64_t)1);
        REQUIRE(events[1].integer == (int64_t)2);
    }
    // BOM-only input: SingleRoot errors at offset 3; MultiValue is empty.
    {
        std::istringstream in(bom());
        StreamReader reader(in);
        JsonEvent ev;
        while (reader.next(ev))
        {
        }
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().code == ErrorCode::UnexpectedEndOfInput);
        REQUIRE(reader.error().offset() == 3);
    }
    {
        std::istringstream in(bom());
        StreamReader reader(in, StreamMode::MultiValue);
        JsonEvent ev;
        REQUIRE(reader.next(ev) == false);
        REQUIRE(!reader.has_error());
    }
    // A BOM after leading whitespace is not at byte 0: rejected, matching
    // parse_copy (which strips only at m_begin).
    {
        const std::string input = " " + bom() + "{}";
        auto ref = parse_copy_result(input);
        REQUIRE(ref.is_err());
        ParseError ref_err = std::move(ref).unwrap_err();

        std::istringstream in(input);
        StreamReader reader(in);
        JsonEvent ev;
        while (reader.next(ev))
        {
        }
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().code == ErrorCode::UnexpectedValueCharacter);
        REQUIRE(reader.error().offset() == ref_err.offset());
        REQUIRE(reader.error().format() == std::string(ref_err.what()));
    }
    // strip_bom off: a byte-0 BOM is an ordinary error at offset 0.
    Config::instance().set_strip_bom(false);
    {
        std::istringstream in(bom() + "1");
        StreamReader reader(in);
        JsonEvent ev;
        while (reader.next(ev))
        {
        }
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().code == ErrorCode::UnexpectedValueCharacter);
        REQUIRE(reader.error().offset() == 0);
    }
}

TEST_CASE("Stream: strict utf8")
{
    ConfigGuard guard;
    Config::instance().set_strict_utf8(true);

    // Valid UTF-8 (e-acute = C3 A9) decodes and rebuilds identically.
    {
        const std::string input = "\"\xC3\xA9\"";
        std::istringstream in(input);
        StreamReader reader(in);
        auto events = collect_events(reader);
        REQUIRE(!reader.has_error());
        REQUIRE(events.size() == 1);
        REQUIRE(events[0].type == EventType::String);
        REQUIRE(events[0].text == "\xC3\xA9");

        auto reference = parse_copy(input);
        Json rebuilt = Json::own(events[0].text, Config::instance().resource());
        REQUIRE(dump(rebuilt) == dump(reference.root()));
    }

    // Ill-formed raw content: first failure code + offset + message match
    // parse_copy exactly (format() is the static table plus offset).
    const std::string raw_cases[] = {
        "\"a\xC0\x80\"",
        "\"a\xFF\"",
        "\"a\xE1"
        "A\"",
        "\"a\xED\xA0\x80\"",
        "\"a\xF4\x90\x80\x80\"",
        "\"a\xE1\"",
    };
    for (const std::string &input : raw_cases)
    {
        CAPTURE(input);
        auto ref = parse_copy_result(input);
        REQUIRE(ref.is_err());
        ParseError ref_err = std::move(ref).unwrap_err();

        std::istringstream in(input);
        StreamReader reader(in, 2);
        JsonEvent ev;
        while (reader.next(ev))
        {
        }
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().offset() == ref_err.offset());
        REQUIRE(reader.error().format() == std::string(ref_err.what()));
    }

    // Escape path: DOM phase 2 feeds the UTF-8 checker before handling the
    // escape, so whichever violation comes first in stream order wins.
    const std::string escape_cases[] = {
        "\"\xE1\\u0041\"", // torn 3-byte lead immediately before the backslash
        "\"\\q\xE1\"",     // invalid escape precedes the bad sequence
        "\"\\n\xE1\"",     // valid escape, then a dangling sequence
    };
    for (const std::string &input : escape_cases)
    {
        CAPTURE(input);
        auto ref = parse_copy_result(input);
        REQUIRE(ref.is_err());
        ParseError ref_err = std::move(ref).unwrap_err();

        std::istringstream in(input);
        StreamReader reader(in, 3);
        JsonEvent ev;
        while (reader.next(ev))
        {
        }
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().offset() == ref_err.offset());
        REQUIRE(reader.error().format() == std::string(ref_err.what()));
    }

    // Object keys go through the same UTF-8 gate as string values.
    {
        const std::string input = "{\"k\xFF\":1}";
        auto ref = parse_copy_result(input);
        REQUIRE(ref.is_err());
        ParseError ref_err = std::move(ref).unwrap_err();

        std::istringstream in(input);
        StreamReader reader(in);
        JsonEvent ev;
        while (reader.next(ev))
        {
        }
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().offset() == ref_err.offset());
        REQUIRE(reader.error().format() == std::string(ref_err.what()));
    }

    // strict_utf8 off: the same bytes are a byte mirror (no error).
    Config::instance().set_strict_utf8(false);
    {
        std::istringstream in("\"a\xFF\"");
        StreamReader reader(in);
        auto events = collect_events(reader);
        REQUIRE(!reader.has_error());
        REQUIRE(events.size() == 1);
        REQUIRE(events[0].text == "a\xFF");
    }
}

TEST_CASE("Stream: error classification parity")
{
    ConfigGuard guard;
    Config::instance().set_strip_bom(false);
    Config::instance().set_strict_utf8(false);
    Config::instance().set_max_depth(Config::kDefaultMaxDepth);

    // A mini golden set covering every parser error family (structure,
    // numbers, strings, literals, BOM). For each, the event core must report
    // the exact same offset and rendered message as parse_copy. The tiny
    // window forces the token classifiers across refill boundaries.
    const char *inputs[] = {
        "1 2",
        "x",
        "[x]",
        "",
        "[1,",
        "\xEF\xBB\xBF{}",
        "{1:2}",
        "{\"a\" 1}",
        "{\"a\":1",
        "{\"a\":1 \"b\":2}",
        "[1 2]",
        "-",
        "01",
        "1.",
        "1e",
        "1e999",
        "\"abc",
        ("\"a\x01"
         "b\""),
        "\"a\\x\"",
        "\"\\uZZZZ\"",
        "\"\\uD800\\u0041\"",
        "\"\\uD800x\"",
        "\"\\uDC00\"",
        "tru",
        "truex",
    };
    for (const char *input : inputs)
    {
        CAPTURE(input);
        auto ref = parse_copy_result(input);
        REQUIRE(ref.is_err());
        ParseError ref_err = std::move(ref).unwrap_err();

        std::istringstream in(input);
        StreamReader reader(in, 2);
        JsonEvent ev;
        while (reader.next(ev))
        {
        }
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().offset() == ref_err.offset());
        REQUIRE(reader.error().format() == std::string(ref_err.what()));
    }
}

TEST_CASE("Stream: items prefix object and array")
{
    const std::string input = "{\"a\":{\"b\":1},\"c\":[10,20,{\"d\":\"x\"}],\"a/b\":5}";

    // A nested object value yields its whole event subtree, MapKey included.
    {
        auto events = collect_item_events(input, "/a");
        const std::vector<OwnedEvent> expected = {
            ev_type(EventType::BeginObject),
            ev_str(EventType::MapKey, "b"),
            ev_int(1),
            ev_type(EventType::EndObject),
        };
        REQUIRE(same_events(events, expected));
    }
    {
        auto events = collect_item_events(input, "/a/b");
        REQUIRE(events.size() == 1);
        REQUIRE(events[0].type == EventType::Integer);
        REQUIRE(events[0].integer == (int64_t)1);
    }
    // Array index selection.
    {
        auto events = collect_item_events(input, "/c/0");
        REQUIRE(events.size() == 1);
        REQUIRE(events[0].integer == (int64_t)10);
    }
    // An object nested in an array: full subtree.
    {
        auto events = collect_item_events(input, "/c/2");
        const std::vector<OwnedEvent> expected = {
            ev_type(EventType::BeginObject),
            ev_str(EventType::MapKey, "d"),
            ev_str(EventType::String, "x"),
            ev_type(EventType::EndObject),
        };
        REQUIRE(same_events(events, expected));
    }
    {
        auto events = collect_item_events(input, "/c/2/d");
        REQUIRE(events.size() == 1);
        REQUIRE(events[0].text == "x");
    }
    // `*` matches every array element.
    {
        auto events = collect_item_events(input, "/c/*");
        const std::vector<OwnedEvent> expected = {
            ev_int(10),
            ev_int(20),
            ev_type(EventType::BeginObject),
            ev_str(EventType::MapKey, "d"),
            ev_str(EventType::String, "x"),
            ev_type(EventType::EndObject),
        };
        REQUIRE(same_events(events, expected));
    }
    // JSON Pointer escape: ~1 decodes to '/'.
    {
        auto events = collect_item_events(input, "/a~1b");
        REQUIRE(events.size() == 1);
        REQUIRE(events[0].integer == (int64_t)5);
    }
    // Non-matching prefixes yield nothing and are not an error.
    for (const char *prefix : {"/nope", "/c/9", "/c/2/nope", "/a/b/c"})
    {
        CAPTURE(prefix);
        auto events = collect_item_events(input, prefix);
        REQUIRE(events.empty());
    }
    // The empty prefix matches the root, i.e. raw iteration.
    {
        std::istringstream raw_in(input);
        StreamReader raw_reader(raw_in, 4);
        auto raw = collect_events(raw_reader);
        auto filtered = collect_item_events(input, "");
        REQUIRE(same_events(filtered, raw));
    }
    // Malformed pointers are rejected when the cursor is constructed.
    {
        std::istringstream in(input);
        StreamReader reader(in);
        REQUIRE_THROWS_AS((void)reader.items("a/b"), std::invalid_argument);
        REQUIRE_THROWS_AS((void)reader.items("/a~2"), std::invalid_argument);
        REQUIRE_THROWS_AS((void)reader.items("/a~"), std::invalid_argument);
    }
}

TEST_CASE("Stream: items multi value prefix")
{
    const std::string input = "{\"a\":1}\n{\"a\":[2,3]}\n{\"b\":4}\n[5]\n";

    std::istringstream in(input);
    StreamReader reader(in, StreamMode::MultiValue, 3);
    StreamItems items = reader.items("/a");
    std::vector<OwnedEvent> events;
    JsonEvent ev;
    while (items.next(ev))
        events.push_back(own_of(ev));
    REQUIRE(!items.has_error());

    const std::vector<OwnedEvent> expected = {
        ev_int(1), ev_type(EventType::BeginArray), ev_int(2), ev_int(3), ev_type(EventType::EndArray),
    };
    REQUIRE(same_events(events, expected));

    // An empty root prefix over a multi-value stream duplicates raw iteration.
    std::istringstream raw_in(input);
    StreamReader raw_reader(raw_in, StreamMode::MultiValue, 3);
    auto raw = collect_events(raw_reader);

    std::istringstream all_in(input);
    StreamReader all_reader(all_in, StreamMode::MultiValue, 3);
    StreamItems all = all_reader.items("");
    std::vector<OwnedEvent> all_events;
    while (all.next(ev))
        all_events.push_back(own_of(ev));
    REQUIRE(!all.has_error());
    REQUIRE(same_events(all_events, raw));
}

TEST_CASE("Stream: push adapter")
{
    const std::string input = "{\"a\":\"x\",\"b\":[1,2]}";

    // Borrowed push: the callback snapshots (copies) text before returning.
    {
        std::istringstream in(input);
        StreamReader reader(in, 2);
        std::vector<OwnedEvent> events;
        const bool clean = reader.for_each_event(
            [&](const JsonEvent &ev)
            {
                events.push_back(own_of(ev));
            });
        REQUIRE(clean);
        REQUIRE(!reader.has_error());
        REQUIRE(events.size() == 9);
        REQUIRE(events[0].type == EventType::BeginObject);
        REQUIRE(events[2].type == EventType::String);
        REQUIRE(events[2].text == "x");
        REQUIRE(events[8].type == EventType::EndObject);
    }
    // Owned push: text may be retained by the callback without copying.
    {
        std::istringstream in(input);
        StreamReader reader(in, 2);
        std::vector<std::string> strings;
        const bool clean = reader.for_each_owned_event(
            [&](const OwnedJsonEvent &ev)
            {
                if (!ev.text.empty())
                    strings.emplace_back(ev.text.data(), ev.text.size());
            });
        REQUIRE(clean);
        const std::vector<std::string> expected = {"a", "x", "b"};
        REQUIRE(strings == expected);
    }
    // Error reporting: the failing pull is not delivered to the callback.
    {
        std::istringstream bad_in("[1 2]");
        StreamReader reader(bad_in);
        size_t calls = 0;
        const bool clean = reader.for_each_event(
            [&](const JsonEvent &)
            {
                ++calls;
            });
        REQUIRE(!clean);
        REQUIRE(reader.has_error());
        REQUIRE(reader.error().code == ErrorCode::ExpectedCommaOrBracket);
        REQUIRE(reader.error().offset() == 3);
        REQUIRE(calls == 2); // BeginArray + Integer 1
    }
    // StreamItems::for_each is the filtered push adapter.
    {
        std::istringstream in("{\"a\":[1,2],\"b\":3}");
        StreamReader reader(in, 2);
        StreamItems items = reader.items("/a");
        std::vector<OwnedEvent> events;
        const bool clean = items.for_each(
            [&](const JsonEvent &ev)
            {
                events.push_back(own_of(ev));
            });
        REQUIRE(clean);
        const std::vector<OwnedEvent> expected = {
            ev_type(EventType::BeginArray),
            ev_int(1),
            ev_int(2),
            ev_type(EventType::EndArray),
        };
        REQUIRE(same_events(events, expected));
    }
}

TEST_CASE("Stream: owned event lifetime")
{
    OwnedJsonEvent first;
    OwnedJsonEvent second;
    {
        std::istringstream in("[\"alpha\",\"beta\"]");
        StreamReader reader(in, 1);
        JsonEvent skip;
        REQUIRE(reader.next(skip));
        REQUIRE(skip.type == EventType::BeginArray);
        REQUIRE(reader.next(first));
        REQUIRE(first.type == EventType::String);
        REQUIRE(first.text == "alpha");
        REQUIRE(reader.next(second));
        REQUIRE(second.type == EventType::String);
        REQUIRE(second.text == "beta");
        // The first string survives the second pull because it is owned.
        REQUIRE(first.text == "alpha");
    }
    // ... and it survives the reader's destruction.
    REQUIRE(first.text == "alpha");
    REQUIRE(second.text == "beta");

    // MapKey text is owned too; non-string events clear the owned buffer.
    {
        std::istringstream in("{\"key\":1}");
        StreamReader reader(in, 1);
        OwnedJsonEvent ev;
        REQUIRE(reader.next(ev));
        REQUIRE(ev.type == EventType::BeginObject);
        REQUIRE(ev.text.empty());
        REQUIRE(reader.next(ev));
        REQUIRE(ev.type == EventType::MapKey);
        REQUIRE(ev.text == "key");
        REQUIRE(reader.next(ev));
        REQUIRE(ev.type == EventType::Integer);
        REQUIRE(ev.integer == (int64_t)1);
        REQUIRE(ev.text.empty());
    }

    // Owned push can retain every string without a separate copy.
    {
        std::istringstream in("[\"one\",\"two\",\"three\"]");
        StreamReader reader(in, 2);
        std::vector<std::string> seen;
        const bool clean = reader.for_each_owned_event(
            [&](const OwnedJsonEvent &ev)
            {
                if (ev.type == EventType::String)
                    seen.emplace_back(ev.text.data(), ev.text.size());
            });
        REQUIRE(clean);
        const std::vector<std::string> expected = {"one", "two", "three"};
        REQUIRE(seen == expected);
    }
}

TEST_CASE("Stream: items error and shell")
{
    std::istringstream in("{\"a\":[1 2]}");
    StreamReader reader(in, 3);
    StreamItems items = reader.items("/a");
    JsonEvent ev;
    REQUIRE(items.next(ev));
    REQUIRE(ev.type == EventType::BeginArray);
    REQUIRE(items.next(ev));
    REQUIRE(ev.integer == (int64_t)1);
    REQUIRE(items.next(ev) == false);
    REQUIRE(items.has_error());
    REQUIRE(items.error().code == ErrorCode::ExpectedCommaOrBracket);
    REQUIRE(items.error().offset() == 8);

    // Throwing shell: a non-matching prefix ends cleanly with nullopt.
    std::istringstream missing_in("{\"z\":1}");
    StreamReader missing_reader(missing_in);
    StreamItems missing = missing_reader.items("/a");
    REQUIRE(!missing.next().has_value());
    REQUIRE(!missing.has_error());

    // Throwing shell: reader failure materialises a ParseError.
    std::istringstream bad_in("[1 2]");
    StreamReader bad_reader(bad_in);
    StreamItems bad = bad_reader.items("");
    REQUIRE(bad.next().has_value()); // BeginArray
    REQUIRE(bad.next().has_value()); // Integer 1
    REQUIRE_THROWS_AS((void)bad.next(), ParseError);
}
