#include <doctest/doctest.h>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory_resource>
#include <string>
#include <string_view>

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
    // Exact allocation-site pin: buffer is content + kPaddingWidth
    // (32-byte content).
    REQUIRE(d2.buffer().size() == 32 + kPaddingWidth);
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
        // Exact allocation-site pin (same 32-byte content + kPaddingWidth)
        REQUIRE(b.buffer().size() == 32 + kPaddingWidth);
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

namespace {
// Deallocating scribbles 0xAB through the block; makes a dangling borrowed
// view deterministically observable without a sanitizer.
class PoisoningResource final : public std::pmr::memory_resource {
public:
    explicit PoisoningResource(
        std::pmr::memory_resource *up = std::pmr::new_delete_resource())
        : m_up(up) {}
private:
    void *do_allocate(std::size_t bytes, std::size_t align) override {
        return m_up->allocate(bytes, align);
    }
    void do_deallocate(void *p, std::size_t bytes, std::size_t align) override {
        std::memset(p, 0xAB, bytes);
        m_up->deallocate(p, bytes, align);
    }
    bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override {
        return this == &other;   // never equal to the pool arena
    }
    std::pmr::memory_resource *m_up;
};
} // namespace

TEST_CASE("Document: move assign foreign-resource source buffer no UAF") {
    PoisoningResource poison;               // outlives src and dst (declared first)
    std::pmr::string buf(&poison);          // source buffer allocator != arena
    // 64 leading spaces push the borrowed value view past glibc's tcache
    // metadata (which overwrites the first 16 bytes of a freed small block),
    // so the 0xAB poison is what the dangling read sees.
    std::string content(64, ' ');
    content += R"(["hello"])";
    buf.assign(content.data(), content.size());
    buf.append(kPaddingWidth, '\0');  // kPaddingWidth NUL tail

    auto src = parse_in_situ(std::move(buf), Storage::Pooled);
    REQUIRE(src.root()[0] == "hello");

    // Non-empty target also exercises the task-02 old-state path.
    Document dst = parse_copy(R"([1])");
    dst = std::move(src);                   // pre-fix: buffer COPIED, source freed+poisoned

    // Pre-fix the array element's borrowed view points into the freed,
    // 0xAB-scribbled source storage -> mismatch (red, no sanitizer needed).
    // Post-fix the storage is stolen and the view is intact (green).
    REQUIRE(dst.buffer().data() != nullptr);
    REQUIRE(dst.root()[0] == "hello");
    REQUIRE(dump(dst) == R"(["hello"])");

    // moved-from source self-contained (task 02 invariant 3, unchanged)
    REQUIRE(src.root().is_null());
    REQUIRE(src.buffer().empty());
}

TEST_CASE("Document: move assign parse_file source no UAF") {
    // R_75's entry-level shape: parse_file's buffer is bound to the default
    // resource while its arena is created inside parse_in_situ, so the source
    // buffer allocator differs from the source arena. Under ASan the dangling
    // borrowed views below are a heap-use-after-free pre-fix; the default
    // resource keeps the bytes readable on a plain build.
    const std::string f = "pjh_doc_move_parse_file.json";
    {
        std::ofstream out(f, std::ios::binary);
        out << R"({"k":"hello","n":1})";
    }

    Document dst;                       // empty target still triggers
    dst = parse_file(f, Storage::Pooled);

    REQUIRE(dst.root()["k"] == "hello");
    REQUIRE(dst.root()["n"] == (int64_t)1);
    REQUIRE(dump(dst) == R"({"k":"hello","n":1})");

    std::remove(f.c_str());
}

TEST_CASE("Document: reset no UAF") {
    auto doc = parse_copy(R"({"a":{"b":[1,2,3]}})");
    doc.reset();
    REQUIRE(doc.root().is_null());
    REQUIRE(doc.buffer().empty());
    REQUIRE(doc.resource() != nullptr);
    REQUIRE(doc.is_view() == false);
    // The internal empty buffer must not be rebound onto the (counted) arena:
    // MSVC debug's per-container _Container_proxy would otherwise persist as
    // an outstanding allocation and trip the Config release probe. Compare
    // the resource() raw pointer, never allocators with operator==.
    REQUIRE(doc.buffer().get_allocator().resource()
            == std::pmr::new_delete_resource());
    // still usable after reset
    doc = parse_copy(R"({"x":1})");
    REQUIRE(doc.root()["x"] == (int64_t)1);

    // Arena (monotonic) variant
    auto adoc = parse_copy(R"({"a":{"b":[1,2,3]}})", Storage::Arena);
    adoc.reset();
    REQUIRE(adoc.root().is_null());
    REQUIRE(adoc.buffer().empty());
    REQUIRE(adoc.resource() != nullptr);
    REQUIRE(adoc.buffer().get_allocator().resource()
            == std::pmr::new_delete_resource());
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

TEST_CASE("Config: resource re-resolved after release") {
    // Warm the fast-path cache with the pre-release resource.
    std::pmr::memory_resource *before = Config::instance().resource();
    REQUIRE(before != nullptr);

    Config::instance().release();

    // Contract: the old pointer is invalidated; the next resource() must
    // re-resolve and hand out a live resource. (Do NOT assert
    // after != before: allocator reuse can legitimately return the same
    // address for the fresh arena, so pointer inequality is not a valid pin.)
    std::pmr::memory_resource *after = Config::instance().resource();
    REQUIRE(after != nullptr);

    {
        // Exercise the re-cached resource through the default-argument path:
        // this allocates from the global resource iff the re-cache is correct.
        Object o;                   // default arg = Config::instance().resource()
        o.insert("k", Json((int64_t)1));
        REQUIRE(o.size() == 1);
    }   // scope exit destroys o through the re-cached resource

    // reset restores defaults and tears the arena down in a quiescent,
    // single-threaded call.
    Config::instance().reset();
    auto doc = parse_copy(R"({"after":"release"})");
    REQUIRE(doc.root()["after"] == "release");
}

TEST_CASE("Config: max depth") {
    // Default: finite DoS guard (secure by default)
    REQUIRE(Config::instance().max_depth() == Config::kDefaultMaxDepth);

    Config::instance().set_max_depth(42);
    REQUIRE(Config::instance().max_depth() == 42);

    Config::instance().set_max_depth(7);
    Config::instance().reset();
    REQUIRE(Config::instance().max_depth() == Config::kDefaultMaxDepth);
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
    REQUIRE(cfg.max_depth() == Config::kDefaultMaxDepth);
    REQUIRE(cfg.strip_bom() == false);
    REQUIRE(cfg.strict_utf8() == false);
    REQUIRE(cfg.storage() == Storage::Pooled);
}
