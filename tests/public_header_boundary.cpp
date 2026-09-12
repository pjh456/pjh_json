#include <doctest/doctest.h>
#include <pjh_json.hpp>
#include <pjh_json/document.hpp>
#include <pjh_json/json.hpp>
#include <pjh_json/parser.hpp>

#include <string>

#ifdef INCLUDE_PJH_JSON_DETAIL_UTILS_HPP
#error "public headers must not include pjh_json/detail/utils.hpp"
#endif
#ifdef INCLUDE_PJH_JSON_DETAIL_LITERAL_HPP
#error "public headers must not include pjh_json/detail/literal.hpp"
#endif
#ifdef INCLUDE_PJH_JSON_DETAIL_WRITER_STATE_HPP
#error "public headers must not include pjh_json/detail/writer_state.hpp"
#endif
#ifdef INCLUDE_PJH_JSON_DETAIL_SIMD_INFO_HPP
#error "public headers must not include pjh_json/detail/simd_info.hpp"
#endif
#ifdef INCLUDE_PJH_JSON_UTILS_HPP
#error "utils.hpp must no longer exist as a public header"
#endif
#ifdef INCLUDE_PJH_JSON_LITERAL_HPP
#error "literal.hpp must no longer exist as a public header"
#endif

using namespace pjh::json;

TEST_CASE("Header: public surface hides internal headers")
{
    static_assert(kPaddingWidth == 128);
    std::string buf = "D83D";
    buf.append(kPaddingWidth, '\0');
    Parser p(buf, Config::instance().resource(), true);
    REQUIRE(p.parse_hex4() == 0xD83Du);
    auto doc = parse_copy(R"({"a":1})");
    REQUIRE(doc.root()["a"].as_int() == (int64_t)1);
}
