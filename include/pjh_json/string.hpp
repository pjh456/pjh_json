#ifndef INCLUDE_PJH_JSON_STRING_HPP
#define INCLUDE_PJH_JSON_STRING_HPP

#include <string_view>
#include <memory_resource>
#include <string>
#include <variant>

namespace pjh::json
{
    /**
     * @brief JSON string: either a view (borrowed) or owned (pmr::string)
     *
     * Avoids unnecessary copies: parsed strings borrow from the parser buffer,
     * while explicitly constructed strings can be either borrowed or owned.
     */
    class String
    {
        std::variant<std::string_view, std::pmr::string> m_data;

    public:
        /**
         * @brief Construct empty (null view)
         */
        String() = default;
        /**
         * @brief Borrowed view (no copy)
         * @param sv Source view -- caller must guarantee lifetime
         * @note Does NOT copy. Call own() to materialise.
         */
        String(std::string_view sv) noexcept : m_data(sv) {}
        /**
         * @brief Borrowed view from C string
         * @param s NUL-terminated source -- caller must guarantee lifetime
         * @note Wraps s in string_view. Does NOT copy.
         */
        String(const char *s) : m_data(std::string_view(s)) {}
        /**
         * @brief Owned string (takes ownership)
         * @param s pmr::string to move into this value
         */
        String(std::pmr::string s) noexcept : m_data(std::move(s)) {}

        /**
         * @brief Implicit conversion to string_view
         * @return View of the string content (always valid regardless of owned/borrowed)
         */
        [[nodiscard]] operator std::string_view() const noexcept
        {
            if (auto *p = std::get_if<std::string_view>(&m_data))
                return *p;
            return std::get<std::pmr::string>(m_data);
        }

        /**
         * @brief true if data is owned (pmr::string), not borrowed
         * @return Whether variant holds pmr::string
         */
        [[nodiscard]] bool is_owned() const noexcept
        {
            return std::holds_alternative<std::pmr::string>(m_data);
        }

        /**
         * @brief Convert view to owned copy if not already owned
         * @param res Memory resource for the copy (default: default_resource)
         * @note Safe to call multiple times; subsequent calls are no-ops.
         */
        void own(std::pmr::memory_resource *res = std::pmr::get_default_resource())
        {
            if (!is_owned())
            {
                auto sv = std::get<std::string_view>(m_data);
                m_data = std::pmr::string(sv, res);
            }
        }

        /**
         * @brief Compare with another String (content only)
         * @param other String to compare
         * @return true if content is identical
         */
        [[nodiscard]] bool operator==(const String &other) const noexcept
        {
            return static_cast<std::string_view>(*this) == static_cast<std::string_view>(other);
        }

        /**
         * @brief Compare with string_view
         * @param other View to compare
         * @return true if content matches
         */
        [[nodiscard]] bool operator==(std::string_view other) const noexcept
        {
            return static_cast<std::string_view>(*this) == other;
        }

        /**
         * @brief Compare with C string
         * @param other NUL-terminated string to compare
         * @return true if content matches
         */
        [[nodiscard]] bool operator==(const char *other) const noexcept
        {
            return static_cast<std::string_view>(*this) == other;
        }
    };
}

#endif
