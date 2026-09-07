#include <doctest/doctest.h>
#include <string>
#include <string_view>

#include <xsimd/xsimd.hpp>

#include <pjh_json/document.hpp>
#include <pjh_json/config.hpp>
#include <pjh_json/writer.hpp>

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

TEST_CASE("Document: move assignment no UAF") {
    auto d1 = parse_copy(R"({"k":[1,2,3],"s":"hello","n":42})");
    auto d2 = parse_copy(R"([1])");
    d2 = std::move(d1);
    REQUIRE(d2.root()["k"].size() == 3);
    REQUIRE(d2.root()["n"] == (int64_t)42);
    REQUIRE(d2.is_view() == false);
    // Exact allocation-site pin: buffer is content + kPaddingWidth, and the
    // contract pins kPaddingWidth to 2x the SIMD batch (32-byte content).
    REQUIRE(d2.buffer().size() == 32 + 2 * xsimd::batch<uint8_t>::size);
    REQUIRE(d1.root().is_null());
    REQUIRE(d1.buffer().empty());

    // control: empty target was already safe, must stay safe
    auto src = parse_copy(R"({"k":[1,2,3],"s":"hello","n":42})");
    Document empty;
    empty = std::move(src);
    REQUIRE(empty.root()["k"].size() == 3);

    // Arena (monotonic) variant
    auto a1 = parse_copy(R"({"k":[1,2,3],"s":"hello","n":42})", Storage::Arena);
    auto a2 = parse_copy(R"([1])", Storage::Arena);
    a2 = std::move(a1);
    REQUIRE(a2.root()["k"].size() == 3);
    REQUIRE(a2.root()["n"] == (int64_t)42);
    REQUIRE(a1.root().is_null());
    REQUIRE(a1.buffer().empty());
}

TEST_CASE("Document: move ctor no UAF") {
    auto a = parse_copy(R"({"k":[1,2,3],"s":"hello","n":42})");
    {
        Document b = std::move(a);   // move-ctor on a non-empty source
        // moved-to is self-contained (semantics intact, dump intact)
        REQUIRE(b.root()["k"].size() == 3);
        REQUIRE(b.root()["n"] == (int64_t)42);
        // Exact allocation-site pin (same 32-byte content + 2x-batch padding)
        REQUIRE(b.buffer().size() == 32 + 2 * xsimd::batch<uint8_t>::size);
        REQUIRE(b.is_view() == false);
        REQUIRE(dump(b) == R"({"k":[1,2,3],"s":"hello","n":42})");
        // moved-from surface state
        REQUIRE(a.root().is_null());
        REQUIRE(a.buffer().empty());
    }   // b dies first -> its arena object is freed here
    // The moved-from buffer allocator must point at the immortal
    // resource, not at b's (now dead) arena.
    // Compare the resource() raw pointer only, never the allocator with
    // operator==: the latter virtual-dispatches do_is_equal on both ends
    // and would UAF on the dead arena by the test itself pre-fix.
    REQUIRE(a.buffer().get_allocator().resource()
            == std::pmr::new_delete_resource());

    // lifecycle control (green pre- and post-fix, pins the order):
    // a as destination, then a as source, both on the rebound buffer
    a.reset();
    auto c = parse_copy(R"([1])");
    a = std::move(c);
    REQUIRE(a.root().size() == 1);
    Document d;
    d = std::move(a);
    REQUIRE(d.root().size() == 1);

    // Arena (monotonic) variant: same pin, same rebind
    auto ea = parse_copy(R"({"k":[1,2,3],"s":"hello","n":42})", Storage::Arena);
    {
        Document eb = std::move(ea);
        REQUIRE(eb.root()["k"].size() == 3);
        REQUIRE(eb.root()["n"] == (int64_t)42);
    }
    REQUIRE(ea.root().is_null());
    REQUIRE(ea.buffer().empty());
    REQUIRE(ea.buffer().get_allocator().resource()
            == std::pmr::new_delete_resource());
}

TEST_CASE("Document: reset no UAF") {
    auto doc = parse_copy(R"({"a":{"b":[1,2,3]}})");
    doc.reset();
    REQUIRE(doc.root().is_null());
    REQUIRE(doc.buffer().empty());
    REQUIRE(doc.resource() != nullptr);
    REQUIRE(doc.is_view() == false);
    // still usable after reset
    doc = parse_copy(R"({"x":1})");
    REQUIRE(doc.root()["x"] == (int64_t)1);

    // Arena (monotonic) variant
    auto adoc = parse_copy(R"({"a":{"b":[1,2,3]}})", Storage::Arena);
    adoc.reset();
    REQUIRE(adoc.root().is_null());
    REQUIRE(adoc.buffer().empty());
    REQUIRE(adoc.resource() != nullptr);
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

TEST_CASE("Config: max depth") {
    // Default: unlimited
    REQUIRE(Config::instance().max_depth() == 0);

    Config::instance().set_max_depth(256);
    REQUIRE(Config::instance().max_depth() == 256);

    Config::instance().set_max_depth(7);
    Config::instance().reset();
    REQUIRE(Config::instance().max_depth() == 0);
}

TEST_CASE("Config: reset restores defaults") {
    Config &cfg = Config::instance();

    cfg.set_strict_duplicate_keys(true);
    cfg.set_arena_block_size(65536);
    cfg.set_max_depth(42);
    cfg.set_strip_bom(true);
    cfg.set_strict_utf8(true);
    cfg.configure(Storage::Arena, 8192);

    cfg.reset();

    REQUIRE(cfg.strict_duplicate_keys() == false);
    REQUIRE(cfg.arena_block_size() == 0);
    REQUIRE(cfg.max_depth() == 0);
    REQUIRE(cfg.strip_bom() == false);
    REQUIRE(cfg.strict_utf8() == false);
    REQUIRE(cfg.storage() == Storage::Pooled);
}
