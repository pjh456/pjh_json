// Must NOT compile: Object::operator[](std::string&&) is deleted.
// Include json.hpp (not object.hpp alone) so this fails on the deleted overload
// rather than on an incomplete Json type.
#include <pjh_json/json.hpp>
#include <string>

void negative_case()
{
    pjh::json::Object o;
    o[std::string("key")] = pjh::json::Json((int64_t)1);
}
