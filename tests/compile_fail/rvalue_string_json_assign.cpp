// Must NOT compile: assigning a std::string temporary into a borrowed String.
#include <pjh_json/json.hpp>
#include <string>

void negative_case()
{
    pjh::json::Json j;
    j = std::string("temporary");
}
