#include "pjh_json/json.hpp"
#include "pjh_json/document.hpp"
#include "pjh_json/writer.hpp"
#include <iostream>
#include <cassert>

int main()
{
    // Write JSON to file
    {
        auto obj = pjh::json::Object::of(
            pjh::json::Object::Entry{"name", "example"},
            pjh::json::Object::Entry{"values", pjh::json::Array::of(10, 20, 30)});
        pjh::json::Json root(std::move(obj));
        pjh::json::dump_file("example_out.json", root, {.pretty = true});
        std::cout << "wrote example_out.json\n";
    }

    // Read back
    {
        auto doc = pjh::json::parse_file("example_out.json");
        const auto &root = doc.root();
        std::cout << "read back name: "
                  << *root["name"].try_as_string() << "\n";
        std::cout << "values[1]: "
                  << root["values"][1].as_int() << "\n";
    }

    // JSONL file
    {
        auto arr = pjh::json::Array::of(
            pjh::json::Object::of(pjh::json::Object::Entry{"id", 1}),
            pjh::json::Object::of(pjh::json::Object::Entry{"id", 2}),
            pjh::json::Object::of(pjh::json::Object::Entry{"id", 3}));
        pjh::json::dump_jsonl_file("example_jsonl.jsonl", arr);
        std::cout << "wrote example_jsonl.jsonl\n";
    }

    // Parse JSONL
    {
        auto jsonl_text = R"({"a":1}
{"a":2}
{"a":3})";
        auto doc = pjh::json::parse_jsonl(jsonl_text);
        std::cout << "parsed " << doc.root().as_array().size()
                  << " jsonl lines\n";
    }
}
