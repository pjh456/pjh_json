// Must NOT compile: Json::operator[](std::string&&) is deleted.
#include <pjh_json/json.hpp>
#include <string>

void negative_case()
{
    pjh::json::Json j;
    j[std::string("key")] = pjh::json::Json((int64_t)1);
}
