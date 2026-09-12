#include "pjh_json/std_interop.hpp"

#include <concepts>
#include <type_traits>
#include <utility>

namespace pjh::json
{
    // Defined out-of-line (after StdValue is complete). Hand-rolled
    // std::visit comparison instead of std::variant::operator==: libstdc++
    // under clang++ instantiates pair<string, StdValue>'s constructor traits
    // from the variant relational path while pair is mid-instantiation
    // (recursive-variant completeness trap). Dispatch on the active
    // alternative and compare directly; the std::vector alternatives recurse
    // back into this declaration.
    bool StdValue::operator==(const StdValue &other) const
    {
        return data.index() == other.data.index() &&
               std::visit(
                   [&other](const auto &lhs) -> bool
                   {
                       using T = std::decay_t<decltype(lhs)>;
                       return lhs == std::get<T>(other.data);
                   },
                   data);
    }

    StdValue to_std(const Json &value)
    {
        if (value.is_null())
            return StdValue{std::monostate{}};
        if (value.is_boolean())
            return StdValue{value.as_boolean()};
        if (value.is_int())
            return StdValue{value.as_int()};
        if (value.is_float())
            return StdValue{value.as_float()};
        if (value.is_string())
            return StdValue{std::string(value.as_string())};
        if (value.is_array())
        {
            StdValue::Array out;
            out.reserve(value.as_array().size());
            for (const Json &e : value.as_array())
                out.push_back(to_std(e));
            return StdValue{std::move(out)};
        }

        StdValue::Object out;
        const Object &o = value.as_object();
        out.reserve(o.size());
        for (const Object::Entry &e : o)
            out.emplace_back(
                std::string(static_cast<std::string_view>(e.first)),
                to_std(e.second));
        return StdValue{std::move(out)};
    }

    Json from_std(const StdValue &value, std::pmr::memory_resource *res)
    {
        if (!res)
            res = Config::instance().resource();
        return std::visit(
            [res](const auto &v) -> Json
            {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::same_as<T, std::monostate>)
                    return nullptr;
                else if constexpr (std::same_as<T, bool>)
                    return v;
                else if constexpr (std::same_as<T, std::int64_t>)
                    return v;
                else if constexpr (std::same_as<T, double>)
                    return v;
                else if constexpr (std::same_as<T, std::string>)
                    return Json::own(v, res);
                else if constexpr (std::same_as<T, StdValue::Array>)
                {
                    Array arr(res);
                    arr.reserve(v.size());
                    for (const StdValue &e : v)
                        arr.push_back(from_std(e, res));
                    return Json(std::move(arr));
                }
                else
                {
                    Object obj(res);
                    for (const auto &[k, val] : v)
                        obj.insert(k, from_std(val, res), res);
                    return Json(std::move(obj));
                }
            },
            value.data);
    }
}
