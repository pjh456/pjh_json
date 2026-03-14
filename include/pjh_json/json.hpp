#ifndef INCLUDE_PJH_JSON_HPP
#define INCLUDE_PJH_JSON_HPP

#include <variant>
#include <cstdint>

#include "array.hpp"
#include "object.hpp"

namespace pjh::json
{

    class Json
    {

    public:
        using variant_t = std::variant<
            std::nullptr_t,
            bool,
            int64_t,
            double,
            std::string,
            Array,
            Object>;

    private:
        variant_t data;

    public:
        Json();

    public:
        bool operator==(const Json &) const noexcept
        {
            return true;
        }
    };

}

#endif // INCLUDE_PJH_JSON_HPP