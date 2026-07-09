#include <iostream>
#include <cassert>
#include <stdexcept>
#include <pjh_json/json.hpp>

using namespace pjh::json;

void simple_value()
{
    std::cout << "Json Value test started." << std::endl;

    Json null_val(nullptr);
    assert(null_val.is_null());

    Json bool_val(true);
    assert(bool_val.is_boolean());
    assert(bool_val.as_boolean());
    assert(bool_val == true);

    Json int_val((int64_t)12);
    assert(int_val.is_int());
    assert(int_val.as_int() == (int64_t)12);
    assert(int_val == (int64_t)12);

    Json float_val((double)1.2);
    assert(float_val.is_float());
    assert(float_val.as_float() == (double)1.2);
    assert(float_val == (double)1.2);

    Json str_val("str");
    assert(str_val.is_string());
    assert(str_val.as_string() == "str");
    assert(str_val == "str");

    std::cout << "Json Value test passed." << std::endl;
}

void array_value()
{
    std::cout << "Json Array test started." << std::endl;

    auto arr1 = std::move(Json(Array{}));
    assert(arr1.empty());
    assert(arr1.is_array());
    assert(arr1.size() == 0);
    try
    {
        auto val = arr1.at(0);
        assert(false);
    }
    catch (...)
    {
    }

    auto arr2 = std::move(Json({Json("pjh"), Json((int64_t)123)}));
    assert(!arr2.empty());
    assert(arr1.is_array());
    assert(arr2.size() == 2);
    assert(arr2[0] == "pjh" && arr2[1] == (int64_t)123);

    // 不同长度的 Array 比较应当为 false
    auto arr3 = std::move(Json({Json((int64_t)1)}));
    assert(arr2 != arr3);

    // erase 越界应当抛异常
    try
    {
        arr2.as_array().erase(5);
        assert(false);
    }
    catch (const std::out_of_range &)
    {
    }

    std::cout << "Json Array test passed." << std::endl;
}

void object_value()
{
    std::cout << "Json Object test started." << std::endl;

    auto obj1 = std::move(Json(Object{}));
    assert(obj1.empty());
    assert(obj1.is_object());
    assert(obj1.size() == 0);

    auto obj2 = std::move(Json(Object({{"pjh", Json((int64_t)123)}, {"123", Json("pjh")}})));
    assert(!obj2.empty());
    assert(obj2.is_object());
    assert(obj2.size() == 2);
    assert(obj2["pjh"] == (int64_t)123 && obj2["123"] == "pjh");

    // 不同长度的 Object 比较应当为 false
    auto obj3 = std::move(Json(Object({{"only", Json((int64_t)1)}})));
    assert(obj2 != obj3);

    std::cout << "Json Object test passed." << std::endl;
}

void test_object_upsert()
{
    std::cout << "Object upsert test started." << std::endl;

    Object obj;
    obj.insert("k", Json((int64_t)1));
    assert(obj.size() == 1);
    obj.insert("k", Json((int64_t)2));
    assert(obj.size() == 1);
    assert(obj["k"] == (int64_t)2);

    std::cout << "Object upsert test passed." << std::endl;
}

void test_object_remove_bool()
{
    std::cout << "Object remove bool test started." << std::endl;

    Object obj;
    obj.insert("k", Json((int64_t)1));
    assert(obj.remove("k") == true);
    assert(obj.remove("k") == false);
    assert(obj.size() == 0);

    std::cout << "Object remove bool test passed." << std::endl;
}

void test_object_content_equality()
{
    std::cout << "Object content equality test started." << std::endl;

    Object a;
    a.insert("a", Json((int64_t)1));
    a.insert("b", Json((int64_t)2));

    Object b;
    b.insert("b", Json((int64_t)2));
    b.insert("a", Json((int64_t)1));
    assert(a == b);

    Object c;
    c.insert("a", Json((int64_t)1));
    c.insert("b", Json((int64_t)99));
    assert(a != c);

    std::cout << "Object content equality test passed." << std::endl;
}

void test_try_as()
{
    std::cout << "try_as test started." << std::endl;

    Json i = Json((int64_t)42);
    assert(i.try_as_int().has_value() && *i.try_as_int() == 42);
    assert(!i.try_as_float().has_value());
    assert(!i.try_as_boolean().has_value());
    assert(!i.try_as_string().has_value());
    assert(i.try_as_array() == nullptr);
    assert(i.try_as_object() == nullptr);

    Json arr = Json({Json((int64_t)1)});
    assert(arr.try_as_array() != nullptr);
    assert(arr.try_as_array()->size() == 1);
    assert(arr.try_as_object() == nullptr);

    Json obj = Json(Object({{"x", Json((int64_t)0)}}));
    assert(obj.try_as_object() != nullptr);
    assert(obj.try_as_array() == nullptr);

    Json s = Json("hi");
    assert(s.try_as_string().has_value() && *s.try_as_string() == "hi");
    assert(!s.try_as_int().has_value());

    Json b = Json(true);
    assert(b.try_as_boolean().has_value() && *b.try_as_boolean() == true);

    Json f = Json(3.14);
    assert(f.try_as_float().has_value() && *f.try_as_float() == 3.14);

    Json n = Json(nullptr);
    assert(!n.try_as_int().has_value());

    std::cout << "try_as test passed." << std::endl;
}
int main()
{
    simple_value();
    array_value();
    object_value();
    test_object_upsert();
    test_object_remove_bool();
    test_object_content_equality();
    test_try_as();
}
