#ifndef INCLUDE_PJH_JSON_EXPECTED_HPP
#define INCLUDE_PJH_JSON_EXPECTED_HPP

#include <variant>

namespace pjh::json
{

    /**
     * @brief Value-or-error holder returned by the *_result entry points
     *
     * In-repo stand-in for std::expected (C++23): the library standard is
     * C++20 and <expected> is unavailable on the stated toolchain floor
     * (GCC 11's libstdc++ has no <expected>; see STACK floor).
     *
     * Minimal slice of std::expected:
     *  - stores T (value) XOR E (error); never default-constructed
     *  - has_value() / value() / error() / explicit operator bool
     *  - no swap, no monadic ops, no emplace; value()/error() carry a
     *    precondition (matching state), not a throw — check has_value()
     *  - T and E must be distinct types (true for every current entry)
     *
     * @note Move-only when T is move-only (Document); copyable when both
     *       alternatives are copyable (pmr::string + JsonError).
     * @note If the project standard ever rises to C++23 this becomes
     *       `using expected = std::expected<T,E>;` in this header; entry
     *       names and signatures stay unchanged (value() then throws
     *       bad_expected_access on the wrong state — stricter, not looser).
     */
    template <class T, class E>
    class expected
    {
    public:
        /**
         * @brief Construct holding the value
         * @param value The stored value (moved in)
         */
        explicit expected(T value)
            : m_v(std::in_place_type<T>, std::move(value))
        {
        }

        /**
         * @brief Construct holding the error
         * @param error The stored error (moved in)
         */
        explicit expected(E error)
            : m_v(std::in_place_type<E>, std::move(error))
        {
        }

        expected(const expected &) = default;
        expected(expected &&) = default;
        expected &operator=(const expected &) = default;
        expected &operator=(expected &&) = default;

        /**
         * @brief true iff the value alternative is engaged
         */
        [[nodiscard]] constexpr bool has_value() const noexcept
        {
            return m_v.index() == 0;
        }

        /**
         * @brief Contextual conversion mirroring has_value()
         */
        [[nodiscard]] constexpr explicit operator bool() const noexcept
        {
            return has_value();
        }

        /**
         * @brief Access the stored value
         * @return Reference to the value alternative
         * @pre has_value()
         */
        [[nodiscard]] T &value() &
        {
            return std::get<T>(m_v);
        }

        /**
         * @brief Access the stored value (const)
         * @return Const reference to the value alternative
         * @pre has_value()
         */
        [[nodiscard]] const T &value() const &
        {
            return std::get<T>(m_v);
        }

        /**
         * @brief Access the stored error
         * @return Reference to the error alternative
         * @pre !has_value()
         */
        [[nodiscard]] E &error() &
        {
            return std::get<E>(m_v);
        }

        /**
         * @brief Access the stored error (const)
         * @return Const reference to the error alternative
         * @pre !has_value()
         */
        [[nodiscard]] const E &error() const &
        {
            return std::get<E>(m_v);
        }

    private:
        std::variant<T, E> m_v;
    };
}

#endif
