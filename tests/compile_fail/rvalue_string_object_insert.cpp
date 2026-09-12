// Must NOT compile: an rvalue std::string key must use insert(key, val, res).
// Include json.hpp (not object.hpp alone) so this fails on the deleted overload
// rather than on an incomplete Json type.
#include <pjh_json/json.hpp>
#include <string>

void negative_case()
{
    pjh::json::Object o;
    o.insert(std::string("key"), pjh::json::Json((int64_t)1));
}
