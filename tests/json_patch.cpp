#include <doctest/doctest.h>

#include <pjh_result/diagnostic.hpp>
#include <pjh_result/result.hpp>

#include <limits>
#include <memory>
#include <memory_resource>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

#include "pjh_json/document.hpp"
#include "pjh_json/error.hpp"
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

    // Parse a JSON literal into a standalone owned Json (global resource).
    Json parse_json(const char *text)
    {
        Document doc = parse_copy(text);
        return doc.root().clone(Config::instance().resource());
    }

    std::string text_of(const Json &value) { return std::string(dump(value)); }

    pjh::result::Result<void, PatchError> apply_text(Json &target,
                                                     const char *patch_text)
    {
        Json patch = parse_json(patch_text);
        return patch_result(target, patch);
    }
}

static_assert(pjh::result::Diagnostic<PatchError>);

TEST_CASE("Patch: parse_json_pointer corpus")
{
    REQUIRE(parse_json_pointer("").empty());

    {
        const JsonPointer p = parse_json_pointer("/a/b");
        REQUIRE(p.size() == 2);
        REQUIRE(p[0] == "a");
        REQUIRE(p[1] == "b");
    }
    {
        const JsonPointer p = parse_json_pointer("/");
        REQUIRE(p.size() == 1);
        REQUIRE(p[0] == "");
    }
    {
        const JsonPointer p = parse_json_pointer("/a/");
        REQUIRE(p.size() == 2);
        REQUIRE(p[0] == "a");
        REQUIRE(p[1] == "");
    }
    {
        const JsonPointer p = parse_json_pointer("/~01");
        REQUIRE(p.size() == 1);
        REQUIRE(p[0] == "~1");
    }
    {
        const JsonPointer p = parse_json_pointer("/~10");
        REQUIRE(p.size() == 1);
        REQUIRE(p[0] == "/0");
    }
    {
        const JsonPointer p = parse_json_pointer("/a~1b");
        REQUIRE(p[0] == "a/b");
    }
    {
        const JsonPointer p = parse_json_pointer("/m~0n");
        REQUIRE(p[0] == "m~n");
    }

    REQUIRE_THROWS_AS((void)parse_json_pointer("a"), std::invalid_argument);
    REQUIRE_THROWS_AS((void)parse_json_pointer("/~2"), std::invalid_argument);
    REQUIRE_THROWS_AS((void)parse_json_pointer("/~"), std::invalid_argument);
    REQUIRE_THROWS_AS((void)parse_json_pointer("~"), std::invalid_argument);
}

TEST_CASE("Patch: add object and array")
{
    // Object add (new key) and overwrite (existing key keeps position).
    {
        Json target = parse_json(R"({"a":1})");
        CHECK(apply_text(target, R"([{"op":"add","path":"/b","value":2},
                                      {"op":"add","path":"/a","value":3}])").is_ok());
        REQUIRE(text_of(target) == R"({"a":3,"b":2})");
    }
    // Array: middle insert, append via index == size, and '-'.
    {
        Json target = parse_json("[1,2,3]");
        CHECK(apply_text(target, R"([{"op":"add","path":"/1","value":9},
                                      {"op":"add","path":"/3","value":8},
                                      {"op":"add","path":"/-","value":7}])").is_ok());
        REQUIRE(text_of(target) == "[1,9,2,8,3,7]");
    }
    // Root add replaces the whole document.
    {
        Json target = parse_json(R"({"a":1})");
        CHECK(apply_text(target, R"([{"op":"add","path":"","value":[1,2]}])").is_ok());
        REQUIRE(text_of(target) == "[1,2]");
    }
    // Empty reference token is a legal empty object key.
    {
        Json target = parse_json("{}");
        CHECK(apply_text(target, R"([{"op":"add","path":"/","value":1}])").is_ok());
        REQUIRE(text_of(target) == R"({"":1})");
    }
    // Escaped key: /a~1b targets the key "a/b"; /m~0n targets "m~n".
    {
        Json target = parse_json("{}");
        CHECK(apply_text(target, R"([{"op":"add","path":"/a~1b","value":1},
                                      {"op":"add","path":"/m~0n","value":2}])").is_ok());
        REQUIRE(target.as_object().at("a/b") == (int64_t)1);
        REQUIRE(target.as_object().at("m~n") == (int64_t)2);
    }
    // Values are owned: the patch document may die before the target is read.
    {
        Json target = parse_json(R"({"a":1})");
        {
            Json patch = parse_json(
                R"([{"op":"add","path":"/s","value":"a-string-longer-than-sso"}])");
            REQUIRE(patch_result(target, patch).is_ok());
        }
        REQUIRE(text_of(target) ==
                R"({"a":1,"s":"a-string-longer-than-sso"})");
    }
    // Array add beyond size -> IndexOutOfRange.
    {
        Json target = parse_json("[1,2]");
        const auto r = apply_text(target, R"([{"op":"add","path":"/3","value":9}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::IndexOutOfRange);
    }
}

TEST_CASE("Patch: remove object and array")
{
    {
        Json target = parse_json(R"({"a":1,"b":2})");
        CHECK(apply_text(target, R"([{"op":"remove","path":"/a"}])").is_ok());
        REQUIRE(text_of(target) == R"({"b":2})");
    }
    // Array remove shifts the tail.
    {
        Json target = parse_json("[1,2,3]");
        CHECK(apply_text(target, R"([{"op":"remove","path":"/1"}])").is_ok());
        REQUIRE(text_of(target) == "[1,3]");
    }
    // Missing key -> PathNotFound.
    {
        Json target = parse_json(R"({"a":1})");
        const auto r = apply_text(target, R"([{"op":"remove","path":"/x"}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::PathNotFound);
    }
    // Root remove -> RootOperation.
    {
        Json target = parse_json(R"({"a":1})");
        const auto r = apply_text(target, R"([{"op":"remove","path":""}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::RootOperation);
    }
    // '-' is only legal for add.
    {
        Json target = parse_json("[1,2,3]");
        const auto r = apply_text(target, R"([{"op":"remove","path":"/-"}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::InvalidArrayIndex);
    }
    // Array index out of range.
    {
        Json target = parse_json("[1,2,3]");
        const auto r = apply_text(target, R"([{"op":"remove","path":"/5"}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::IndexOutOfRange);
    }
}

TEST_CASE("Patch: replace")
{
    {
        Json target = parse_json(R"({"a":1,"b":2})");
        CHECK(apply_text(target, R"([{"op":"replace","path":"/a","value":9}])").is_ok());
        REQUIRE(text_of(target) == R"({"a":9,"b":2})");
    }
    {
        Json target = parse_json("[1,2,3]");
        CHECK(apply_text(target, R"([{"op":"replace","path":"/1","value":9}])").is_ok());
        REQUIRE(text_of(target) == "[1,9,3]");
    }
    {
        Json target = parse_json(R"({"a":1})");
        CHECK(apply_text(target, R"([{"op":"replace","path":"","value":{"b":2}}])").is_ok());
        REQUIRE(text_of(target) == R"({"b":2})");
    }
    // replace requires the target to exist (unlike add).
    {
        Json target = parse_json(R"({"a":1})");
        const auto r = apply_text(target, R"([{"op":"replace","path":"/x","value":2}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::PathNotFound);
    }
    // Leading zero is not a valid array index.
    {
        Json target = parse_json("[1,2,3]");
        const auto r = apply_text(target, R"([{"op":"replace","path":"/01","value":9}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::InvalidArrayIndex);
    }
}

TEST_CASE("Patch: move semantics")
{
    // Basic object move.
    {
        Json target = parse_json(R"({"a":1,"b":2})");
        CHECK(apply_text(target, R"([{"op":"move","from":"/a","path":"/c"}])").is_ok());
        REQUIRE(text_of(target) == R"({"b":2,"c":1})");
    }
    // Same-array remove-then-add: [a,b,c] move /0 -> /2 == [b,c,a].
    {
        Json target = parse_json(R"(["a","b","c"])");
        CHECK(apply_text(target, R"([{"op":"move","from":"/0","path":"/2"}])").is_ok());
        REQUIRE(text_of(target) == R"(["b","c","a"])");
    }
    // Move to root is a wholesale replace.
    {
        Json target = parse_json(R"({"a":{"x":1},"b":2})");
        CHECK(apply_text(target, R"([{"op":"move","from":"/a","path":""}])").is_ok());
        REQUIRE(text_of(target) == R"({"x":1})");
    }
    // from == path is a defined no-op.
    {
        Json target = parse_json(R"({"a":1,"b":2})");
        CHECK(apply_text(target, R"([{"op":"move","from":"/a","path":"/a"}])").is_ok());
        REQUIRE(text_of(target) == R"({"a":1,"b":2})");
    }
    // from == path at a missing location still fails: RFC 6902 4.4 requires
    // "from" to exist even when the move is a self-target no-op.
    {
        Json target = parse_json(R"({"a":1})");
        const auto r = apply_text(
            target, R"([{"op":"move","from":"/missing","path":"/missing"}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::PathNotFound);
        REQUIRE(r.unwrap_err().pointer() == "/missing");
        REQUIRE(text_of(target) == R"({"a":1})");
    }
    // from missing -> PathNotFound, pointer is the from pointer.
    {
        Json target = parse_json(R"({"a":1})");
        const auto r = apply_text(target, R"([{"op":"move","from":"/x","path":"/b"}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::PathNotFound);
        REQUIRE(r.unwrap_err().pointer() == "/x");
    }
    // from is a proper prefix of path -> MoveIntoDescendant.
    {
        Json target = parse_json(R"({"a":{"x":1}})");
        const auto r = apply_text(
            target, R"([{"op":"move","from":"/a","path":"/a/b"}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::MoveIntoDescendant);
        REQUIRE(text_of(target) == R"({"a":{"x":1}})");
    }
    // Moving the root is illegal.
    {
        Json target = parse_json(R"({"a":1})");
        const auto r = apply_text(target, R"([{"op":"move","from":"","path":"/b"}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::RootOperation);
    }
}

TEST_CASE("Patch: copy")
{
    {
        Json target = parse_json(R"({"a":{"x":1}})");
        CHECK(apply_text(target, R"([{"op":"copy","from":"/a","path":"/b"}])").is_ok());
        REQUIRE(text_of(target) == R"({"a":{"x":1},"b":{"x":1}})");
    }
    // The copy is an independent clone: a later op on /b does not touch /a.
    {
        Json target = parse_json(R"({"a":{"x":1}})");
        CHECK(apply_text(
            target, R"([{"op":"copy","from":"/a","path":"/b"},
                        {"op":"add","path":"/b/x","value":2}])").is_ok());
        REQUIRE(target.as_object().at("a").as_object().at("x") == (int64_t)1);
        REQUIRE(target.as_object().at("b").as_object().at("x") == (int64_t)2);
    }
    // Copy of the root into a member: the clone is independent of the source.
    {
        Json target = parse_json(R"({"a":1})");
        CHECK(apply_text(target, R"([{"op":"copy","from":"","path":"/b"}])").is_ok());
        REQUIRE(text_of(target) == R"({"a":1,"b":{"a":1}})");
    }
    // Copy to the root replaces the whole document.
    {
        Json target = parse_json(R"({"a":{"x":1}})");
        CHECK(apply_text(target, R"([{"op":"copy","from":"/a","path":""}])").is_ok());
        REQUIRE(text_of(target) == R"({"x":1})");
    }
    // from missing -> PathNotFound.
    {
        Json target = parse_json(R"({"a":1})");
        const auto r = apply_text(target, R"([{"op":"copy","from":"/x","path":"/b"}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::PathNotFound);
    }
}

TEST_CASE("Patch: test equality")
{
    // Object equality is order-insensitive.
    {
        Json target = parse_json(R"({"a":1,"b":2})");
        CHECK(apply_text(target, R"([{"op":"test","path":"","value":{"b":2,"a":1}}])").is_ok());
    }
    // Array equality is order-sensitive.
    {
        Json target = parse_json("[1,2]");
        const auto r = apply_text(target, R"([{"op":"test","path":"","value":[2,1]}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::TestFailed);
    }
    // Integer 1 and floating 1.0 are different JSON kinds here.
    {
        Json target = parse_json(R"({"x":1})");
        const auto r = apply_text(target, R"([{"op":"test","path":"/x","value":1.0}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::TestFailed);
    }
    // Root test succeeds on an equal document.
    {
        Json target = parse_json(R"({"a":[1,2]})");
        CHECK(apply_text(target, R"([{"op":"test","path":"","value":{"a":[1,2]}}])").is_ok());
    }
    // Test failure keeps the target untouched.
    {
        Json target = parse_json(R"({"a":1})");
        const std::string before = text_of(target);
        const auto r = apply_text(target, R"([{"op":"test","path":"/a","value":2}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::TestFailed);
        REQUIRE(text_of(target) == before);
    }
    // NaN is never equal to NaN.
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        Json target(nan);
        Array ops;
        Object op;
        op.insert("op", Json("test"));
        op.insert("path", Json(""));
        op.insert("value", Json(nan));
        ops.push_back(Json(std::move(op)));
        Json patch(std::move(ops));

        const auto r = patch_result(target, patch);
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::TestFailed);
    }
}

TEST_CASE("Patch: atomicity")
{
    // A later failing test must leave the target byte-identical.
    {
        Json target = parse_json(R"({"a":1})");
        const std::string before = text_of(target);
        const auto r = apply_text(
            target, R"([{"op":"add","path":"/a","value":2},
                        {"op":"test","path":"/a","value":999}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::TestFailed);
        REQUIRE(text_of(target) == before);
    }
    // Mid-array move followed by a failure: still untouched.
    {
        Json target = parse_json("[1,2,3]");
        const std::string before = text_of(target);
        const auto r = apply_text(
            target, R"([{"op":"move","from":"/0","path":"/2"},
                        {"op":"test","path":"/0","value":999}])");
        REQUIRE(r.is_err());
        REQUIRE(text_of(target) == before);
    }
    // Empty patch is Ok and changes nothing.
    {
        Json target = parse_json(R"({"a":1})");
        CHECK(apply_text(target, "[]").is_ok());
        REQUIRE(text_of(target) == R"({"a":1})");
    }
    // Malformed patch document does not touch the target either.
    {
        Json target = parse_json(R"({"a":1})");
        const std::string before = text_of(target);
        const auto r = apply_text(target, R"([{"op":"frobnicate","path":"/a"}])");
        REQUIRE(r.is_err());
        REQUIRE(text_of(target) == before);
    }
}

TEST_CASE("Patch: structured errors")
{
    {
        Json target = parse_json("{}");
        const auto r = apply_text(
            target, R"([{"op":"frobnicate","path":"/a"}])");
        REQUIRE(r.is_err());
        const PatchError &e = r.unwrap_err();
        REQUIRE(e.code() == PatchErrorKind::UnknownOp);
        REQUIRE(e.op_index() == 0);
        REQUIRE(e.op() == "frobnicate");
        REQUIRE(e.pointer() == "/a");
        REQUIRE(e.category() == Category::Patch);
        REQUIRE(e.kind() == Category::Patch);
        REQUIRE(patch_error_kind_name(e.code()) == "UnknownOp");
        REQUIRE(pjh::result::render(e) == "unknown patch op: frobnicate");
        REQUIRE(e.message() == std::string_view("unknown patch op: frobnicate"));
    }
    // op_index points at the failing operation, not the first one.
    {
        Json target = parse_json(R"({"a":1})");
        const auto r = apply_text(
            target, R"([{"op":"add","path":"/b","value":2},
                        {"op":"test","path":"/a","value":9}])");
        REQUIRE(r.is_err());
        const PatchError &e = r.unwrap_err();
        REQUIRE(e.code() == PatchErrorKind::TestFailed);
        REQUIRE(e.op_index() == 1);
        REQUIRE(e.op() == "test");
        REQUIRE(e.pointer() == "/a");
    }
    // Invalid pointer grammar is caught by the pre-scan.
    {
        Json target = parse_json("{}");
        const auto r = apply_text(target, R"([{"op":"add","path":"bad","value":1}])");
        REQUIRE(r.is_err());
        const PatchError &e = r.unwrap_err();
        REQUIRE(e.code() == PatchErrorKind::InvalidPointer);
        REQUIRE(e.op() == "add");
        REQUIRE(e.pointer() == "bad");
    }
    // Scalar parent -> TypeMismatch.
    {
        Json target = parse_json(R"({"a":1})");
        const auto r = apply_text(target, R"([{"op":"add","path":"/a/b","value":1}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::TypeMismatch);
    }
}

TEST_CASE("Patch: resource accounting")
{
    TestCountingResource counter;
    Json target = parse_json(R"({"a":1})");
    Json patch = parse_json(
        R"([{"op":"add","path":"/b","value":{"k":[1,2,3]}}])");

    REQUIRE(patch_result(target, patch, &counter).is_ok());
    REQUIRE(counter.outstanding() > 0);

    target = Json(); // destroy the patched tree: all new nodes free into counter
    REQUIRE(counter.outstanding() == 0);

    // Passing doc.resource() keeps new nodes inside the document arena.
    Document doc = parse_copy(R"({"a":1})");
    Json member_patch = parse_json(R"([{"op":"add","path":"/b","value":2}])");
    REQUIRE(patch_result(doc.root(), member_patch, doc.resource()).is_ok());
    REQUIRE(doc.root().as_object().at("b") == (int64_t)2);
}

TEST_CASE("Patch: malformed patch document")
{
    // Top level is not an array.
    {
        Json target = parse_json("{}");
        const auto r = apply_text(target, R"({"op":"add"})");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::InvalidPatchDocument);
    }
    // Operation element is not an object.
    {
        Json target = parse_json("{}");
        const auto r = apply_text(target, "[42]");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::InvalidPatchDocument);
    }
    // Missing "op".
    {
        Json target = parse_json("{}");
        const auto r = apply_text(target, R"([{"path":"/a","value":1}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::InvalidPatchDocument);
    }
    // "op" is not a string.
    {
        Json target = parse_json("{}");
        const auto r = apply_text(target, R"([{"op":1,"path":"/a","value":1}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::InvalidPatchDocument);
    }
    // Missing "path".
    {
        Json target = parse_json("{}");
        const auto r = apply_text(target, R"([{"op":"add","value":1}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::InvalidPatchDocument);
    }
    // Missing "value" for add.
    {
        Json target = parse_json("{}");
        const auto r = apply_text(target, R"([{"op":"add","path":"/a"}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::InvalidPatchDocument);
    }
    // Missing "from" for move.
    {
        Json target = parse_json("{}");
        const auto r = apply_text(target, R"([{"op":"move","path":"/a"}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::InvalidPatchDocument);
    }
    // Bad pointer grammar in "from".
    {
        Json target = parse_json("{}");
        const auto r = apply_text(target, R"([{"op":"move","from":"x","path":"/a"}])");
        REQUIRE(r.is_err());
        REQUIRE(r.unwrap_err().code() == PatchErrorKind::InvalidPointer);
        REQUIRE(r.unwrap_err().pointer() == "x");
    }
}

TEST_CASE("Patch: throwing shell")
{
    Json target = parse_json(R"({"a":1})");
    Json good = parse_json(R"([{"op":"add","path":"/b","value":2}])");
    REQUIRE_NOTHROW(patch(target, good));
    REQUIRE(text_of(target) == R"({"a":1,"b":2})");

    Json bad = parse_json(R"([{"op":"test","path":"/a","value":9}])");
    bool caught = false;
    try
    {
        patch(target, bad);
    }
    catch (const PatchError &e)
    {
        caught = true;
        REQUIRE(e.code() == PatchErrorKind::TestFailed);
        REQUIRE(e.op_index() == 0);
        REQUIRE(e.op() == "test");
        REQUIRE(e.pointer() == "/a");
        REQUIRE(e.category() == Category::Patch);
    }
    REQUIRE(caught);
}
