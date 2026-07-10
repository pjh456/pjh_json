#include "pjh_json/json.hpp"
#include "pjh_json/parser.hpp"
#include "pjh_json/writer.hpp"
#include <iostream>

int main()
{
    // Parse JSON string
    std::string_view json = R"({"name":"pjh_json","version":1,"active":true,"data":null})";
    auto doc = pjh::json::parse_copy(json);

    const auto &root = doc.root();

    // Type checks
    std::cout << "is_object: " << root.is_object() << "\n";
    std::cout << "version is_int: " << root["version"].is_int() << "\n";
    std::cout << "active is_boolean: " << root["active"].is_boolean() << "\n";
    std::cout << "data is_null: " << root["data"].is_null() << "\n";

    // Safe access
    auto ver = root["version"].try_as_int();
    if (ver)
        std::cout << "version = " << *ver << "\n";

    auto name = root["name"].try_as_string();
    if (name)
        std::cout << "name = " << *name << "\n";

    // Dump back to string
    auto out = pjh::json::dump(doc);
    std::cout << "dumped: " << out << "\n";
}
