#include "pjh_json/json.hpp"
#include "pjh_json/document.hpp"
#include "pjh_json/config.hpp"
#include "pjh_json/parser.hpp"
#include "pjh_json/writer.hpp"
#include <iostream>
#include <memory_resource>

int main()
{
    // 1. Use global config with Arena (monotonic buffer) policy
    pjh::json::Config::instance().configure(
        pjh::json::Storage::Arena, 8192);

    std::string_view json = R"({"items":[1,2,3,4,5],"meta":{"count":5}})";
    auto doc = pjh::json::parse_copy(json);
    std::cout << "arena-allocated: " << pjh::json::dump(doc) << "\n";

    // 2. Per-document with custom block size
    auto doc2 = pjh::json::parse_copy(
        R"({"large":"value"})",
        pjh::json::Storage::Pooled);
    std::cout << "pooled: " << pjh::json::dump(doc2) << "\n";

    // 3. Parse in-situ (moves buffer ownership, zero-copy strings)
    std::pmr::string buf("{\"hello\":\"world\"}", std::pmr::get_default_resource());
    buf.append(64, '\0'); // SIMD padding
    auto doc3 = pjh::json::parse_in_situ(std::move(buf));
    std::cout << "in-situ: " << pjh::json::dump(doc3) << "\n";

    // Reset global config
    pjh::json::Config::instance().reset();
}
