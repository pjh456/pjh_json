#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <memory_resource>
#include <string>
#include <string_view>
#include <utility>

#include "pjh_json/document.hpp"
#include "pjh_json/json.hpp"
#include "pjh_json/patch.hpp"
#include "pjh_json/writer.hpp"

using namespace pjh::json;

namespace
{
    // Local counting resource (the library's src/counting_resource.hpp is
    // private — not on the test include path). Upstream must be a
    // make_unique'd pool resource: never unique_ptr<new_delete_resource>().
    struct TestCountingResource : std::pmr::memory_resource
    {
        explicit TestCountingResource()
            : m_up(std::make_unique<std::pmr::unsynchronized_pool_resource>())
        {
        }

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
        bool do_is_equal(const std::pmr::memory_resource &other) const noexcept override { return this == &other; }

    private:
        std::unique_ptr<std::pmr::memory_resource> m_up;
        long long m_outstanding = 0;
    };

    // Parse a JSON literal into a standalone owned Json (global resource).
    Json parse_json(const char *text)
    {
        Document doc = parse_copy(text);
        return doc.root().clone(Config::instance().resource());
    }

    std::string text_of(const Json &value)
    {
        return std::string(dump(value));
    }
}

TEST_CASE("MergePatch: rfc7386 examples")
{
    struct Example
    {
        const char *original;
        const char *patch;
        const char *result;
    };
    // RFC 7386 Appendix A, verbatim.
    const Example examples[] = {
        {R"({"a":"b"})", R"({"a":"c"})", R"({"a":"c"})"},
        {R"({"a":"b"})", R"({"b":"c"})", R"({"a":"b","b":"c"})"},
        {R"({"a":"b"})", R"({"a":null})", "{}"},
        {R"({"a":"b","b":"c"})", R"({"a":null})", R"({"b":"c"})"},
        {R"({"a":["b"]})", R"({"a":"c"})", R"({"a":"c"})"},
        {R"({"a":"c"})", R"({"a":["b"]})", R"({"a":["b"]})"},
        {R"({"a":{"b":"c"}})", R"({"a":{"b":"d","c":null}})", R"({"a":{"b":"d"}})"},
        {R"({"a":[{"b":"c"}]})", R"({"a":[1]})", R"({"a":[1]})"},
        {R"(["a","b"])", R"(["c","d"])", R"(["c","d"])"},
        {R"({"a":"b"})", R"(["c"])", R"(["c"])"},
        {R"({"a":"foo"})", "null", "null"},
        {R"({"a":"foo"})", R"("bar")", R"("bar")"},
        {R"({"e":null})", R"({"a":1})", R"({"e":null,"a":1})"},
        {"[1,2]", R"({"a":"b","c":null})", R"({"a":"b"})"},
        {"{}", R"({"a":{"bb":{"ccc":null}}})", R"({"a":{"bb":{}}})"},
    };

    for (const Example &e : examples)
    {
        Json target = parse_json(e.original);
        const Json patch = parse_json(e.patch);
        merge_patch(target, patch);

        REQUIRE(text_of(target) == e.result);       // byte order / kinds
        const Json expected = parse_json(e.result); // deep equality
        REQUIRE(target == expected);
    }
}

TEST_CASE("MergePatch: null deletes")
{
    // A null for a missing key is a no-op.
    {
        Json target = parse_json(R"({"a":1})");
        const Json patch = parse_json(R"({"zzz":null})");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == R"({"a":1})");
    }
    // An existing key is deleted, not overwritten with null.
    {
        Json target = parse_json(R"({"a":1,"b":2})");
        const Json patch = parse_json(R"({"b":null})");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == R"({"a":1})");
        REQUIRE_FALSE(target.as_object().contains("b"));
    }
    // Nested null recursion deletes only the named member.
    {
        Json target = parse_json(R"({"a":{"b":1,"c":2},"d":3})");
        const Json patch = parse_json(R"({"a":{"b":null}})");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == R"({"a":{"c":2},"d":3})");
    }
    // A null-only patch over a non-object target still coerces to an (empty)
    // object: the delete has nothing to remove.
    {
        Json target = parse_json("[1,2,3]");
        const Json patch = parse_json(R"({"x":null})");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == "{}");
    }
}

TEST_CASE("MergePatch: non-object replaces")
{
    // Array, scalar and null patches replace the whole target.
    {
        Json target = parse_json(R"({"a":1})");
        const Json patch = parse_json("[4,5]");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == "[4,5]");
    }
    {
        Json target = parse_json(R"({"a":1})");
        const Json patch = parse_json("42");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == "42");
    }
    {
        Json target = parse_json(R"({"a":1})");
        const Json patch = parse_json("null");
        merge_patch(target, patch);
        REQUIRE(target.is_null());
    }
    {
        Json target = parse_json("7");
        const Json patch = parse_json(R"("s")");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == R"("s")");
    }
    // An object patch coerces a non-object target to an object.
    {
        Json target = parse_json("[1,2]");
        const Json patch = parse_json(R"({"a":1})");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == R"({"a":1})");
    }
    // Arrays are opaque: an array member is replaced, never element-merged.
    {
        Json target = parse_json(R"({"a":[1,2,3]})");
        const Json patch = parse_json(R"({"a":[9]})");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == R"({"a":[9]})");
    }
    // The wholesale replacement is an owned clone: the patch may die.
    {
        Json target = parse_json(R"({"a":1})");
        {
            Json patch = parse_json(R"(["a-string-longer-than-sso-xxxx"])");
            merge_patch(target, patch);
        }
        REQUIRE(text_of(target) == R"(["a-string-longer-than-sso-xxxx"])");
    }
}

TEST_CASE("MergePatch: ownership and resource")
{
    // New keys are owned into the target resource: the patch may die before
    // the merged key is read.
    {
        Json target = parse_json(R"({"a":1})");
        {
            Json patch = parse_json(R"({"a-key-longer-than-sso":2})");
            merge_patch(target, patch);
        }
        REQUIRE(text_of(target) == R"({"a":1,"a-key-longer-than-sso":2})");
        REQUIRE(target.as_object().at("a-key-longer-than-sso") == (int64_t)2);
    }
    // New nested values are cloned into the target resource too.
    {
        Json target = parse_json("{}");
        {
            Json patch = parse_json(R"({"k":{"s":"a-string-longer-than-sso-xxxx"}})");
            merge_patch(target, patch);
        }
        REQUIRE(target.as_object().at("k").as_object().at("s") == std::string_view("a-string-longer-than-sso-xxxx"));
    }
    // Every created node allocates from the passed resource; destroying the
    // target returns the outstanding count to zero.
    {
        TestCountingResource counter;
        Json target = parse_json(R"({"a":1})");
        Json patch = parse_json(R"({"b":{"c":"a-string-longer-than-sso-xxxx"},"d":[1,2,3]})");

        merge_patch(target, patch, &counter);
        REQUIRE(counter.outstanding() > 0);
        REQUIRE(target.as_object().at("b").as_object().at("c") == std::string_view("a-string-longer-than-sso-xxxx"));
        REQUIRE(target.as_object().at("d").as_array().size() == 3);

        target = Json(); // destroy the merged tree: all nodes free
        REQUIRE(counter.outstanding() == 0);
    }
    // Passing doc.resource() keeps new nodes inside the document arena.
    {
        Document doc = parse_copy(R"({"a":1})");
        const Json patch = parse_json(R"({"b":2})");
        merge_patch(doc.root(), patch, doc.resource());
        REQUIRE(doc.root().as_object().at("b") == (int64_t)2);
    }
}

TEST_CASE("MergePatch: empty patch")
{
    // An empty object patch leaves both a populated and an empty target
    // unchanged.
    {
        Json target = parse_json(R"({"a":1})");
        const Json patch = parse_json("{}");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == R"({"a":1})");
    }
    {
        Json target = parse_json("{}");
        const Json patch = parse_json("{}");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == "{}");
    }
    // An object patch against an empty target just copies the members.
    {
        Json target = parse_json("{}");
        const Json patch = parse_json(R"({"a":1,"b":{"c":2}})");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == R"({"a":1,"b":{"c":2}})");
    }
    // An empty object patch coerces a null target to an empty object.
    {
        Json target;
        const Json patch = parse_json("{}");
        merge_patch(target, patch);
        REQUIRE(text_of(target) == "{}");
    }
}
