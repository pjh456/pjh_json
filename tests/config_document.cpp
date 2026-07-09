#include <iostream>
#include <cassert>
#include <string>
#include <string_view>

#include <pjh_json/document.hpp>
#include <pjh_json/config.hpp>

using namespace pjh::json;

void test_document_owns_memory()
{
    std::cout << "Document ownership test started." << std::endl;

    auto d1 = parse_copy(R"({"a":1,"b":[1,2,3]})");
    auto d2 = parse_copy(R"(["x","y","z"])");
    assert(d1.root()["a"] == (int64_t)1);
    assert(d2.root()[0] == "x");

    Document moved = std::move(d1);
    assert(moved.root()["b"].size() == 3);

    std::cout << "Document ownership test passed." << std::endl;
}

void test_storage_policy()
{
    std::cout << "Storage policy test started." << std::endl;

    auto arena_doc = parse_copy(R"({"k":"v"})", Storage::Arena);
    assert(arena_doc.root()["k"] == "v");

    auto pooled_doc = parse_copy(R"([1,2])", Storage::Pooled);
    assert(pooled_doc.root().size() == 2);

    auto sys_doc = parse_copy(R"(true)", Storage::SystemDefault);
    assert(sys_doc.root() == true);

    std::cout << "Storage policy test passed." << std::endl;
}

void test_config_default_storage()
{
    std::cout << "Config default storage test started." << std::endl;

    Config::instance().configure(Storage::Arena, 8192);
    assert(Config::instance().storage() == Storage::Arena);

    auto doc = parse_copy(R"({"x":1})");
    assert(doc.root()["x"] == (int64_t)1);

    Config::instance().configure(Storage::Pooled, 4096);
    assert(Config::instance().storage() == Storage::Pooled);

    std::cout << "Config default storage test passed." << std::endl;
}

void test_config_release()
{
    std::cout << "Config release test started." << std::endl;

    {
        Object o;
        o.insert("k", Json((int64_t)1));
        assert(o.size() == 1);
    }

    Config::instance().release();
    Config::instance().reset();

    auto doc = parse_copy(R"({"after":"release"})");
    assert(doc.root()["after"] == "release");

    std::cout << "Config release test passed." << std::endl;
}

int main()
{
    std::cout << "--- Starting Config / Document Tests ---" << std::endl;

    test_document_owns_memory();
    test_storage_policy();
    test_config_default_storage();
    test_config_release();

    std::cout << "--- All Config / Document Tests Passed Successfully! ---" << std::endl;
    return 0;
}
