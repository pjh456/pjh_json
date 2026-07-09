#ifndef INCLUDE_PJH_JSON_STRING_HPP
#define INCLUDE_PJH_JSON_STRING_HPP

#include <string_view>
#include <memory_resource>
#include <string>
#include <variant>

namespace pjh::json
{
    class String
    {
        std::variant<std::string_view, std::pmr::string> m_data;

    public:
        String() = default;
        String(std::string_view sv) noexcept : m_data(sv) {}
        String(const char *s) : m_data(std::string_view(s)) {}
        String(std::pmr::string s) noexcept : m_data(std::move(s)) {}

        [[nodiscard]] operator std::string_view() const noexcept
        {
            if (auto *p = std::get_if<std::string_view>(&m_data))
                return *p;
            return std::get<std::pmr::string>(m_data);
        }

        [[nodiscard]] bool is_owned() const noexcept
        {
            return std::holds_alternative<std::pmr::string>(m_data);
        }

        void own(std::pmr::memory_resource *res = std::pmr::get_default_resource())
        {
            if (!is_owned())
            {
                auto sv = std::get<std::string_view>(m_data);
                m_data = std::pmr::string(sv, res);
            }
        }

        [[nodiscard]] bool operator==(const String &other) const noexcept
        {
            return static_cast<std::string_view>(*this) == static_cast<std::string_view>(other);
        }

        [[nodiscard]] bool operator==(std::string_view other) const noexcept
        {
            return static_cast<std::string_view>(*this) == other;
        }

        [[nodiscard]] bool operator==(const char *other) const noexcept
        {
            return static_cast<std::string_view>(*this) == other;
        }
    };
}

#endif
