#ifndef INCLUDE_PJH_JSON_ARRAY_HPP
#define INCLUDE_PJH_JSON_ARRAY_HPP

#include <algorithm>
#include <memory>
#include <memory_resource>
#include <utility>
#include <vector>
#include <concepts>

#include "json_fwd.hpp"
#include "config.hpp"

namespace pjh::json
{
    class Json;

    /**
     * @brief JSON array type (vector of Json values)
     *
     * Wraps std::pmr::vector<Json>. Copy disabled -- use clone().
     */
    class Array
    {
    public:
        using Vec = std::pmr::vector<Json>;

    private:
        Vec m_data;
        std::pmr::memory_resource *m_resource{nullptr};

        friend class Json;

    public:
        /**
         * @brief Construct empty array with given allocator
         * @param res Memory resource for elements (default: global config resource)
         */
        Array(
            std::pmr::memory_resource *res = Config::instance().resource());

        /**
         * @brief Construct from existing vector (adopts allocator)
         * @param vec Pre-populated vector; allocator is inferred from vec
         */
        Array(Vec vec);

        ~Array() = default;

        /**
         * @brief Copy not allowed -- use clone()
         */
        Array(const Array &) = delete;
        /**
         * @brief Copy not allowed -- use clone()
         */
        Array &operator=(const Array &) = delete;

        /**
         * @brief Move construct
         * @param other Source array (left empty)
         */
        Array(Array &&) noexcept;
        /**
         * @brief Move assign
         * @param other Source array (left empty)
         * @return *this
         */
        Array &operator=(Array &&) noexcept;

        /**
         * @brief Deep copy into specified memory resource
         * @param into Allocator for copied elements (default: global config resource)
         * @return Independent deep copy
         */
        [[nodiscard]] Array clone(
            std::pmr::memory_resource *into = Config::instance().resource()) const;

        /**
         * @brief Construct array from variadic values
         * @tparam Ts Types constructible as Json
         * @param vals Values to populate (forwarded to Json ctors)
         * @return Array containing all vals in order
         */
        template <class... Ts>
            requires(std::constructible_from<Json, Ts> && ...)
        [[nodiscard]] static Array of(Ts &&...vals);

    public:
        /**
         * @brief Element count
         * @return Number of elements
         */
        [[nodiscard]] size_t size() const noexcept;

        /**
         * @brief true if no elements
         */
        [[nodiscard]] bool empty() const noexcept;

        /**
         * @brief Remove all elements
         */
        void clear() noexcept;

        /**
         * @brief true if value exists in array
         * @param val Value to search (linear scan)
         * @return true if equal element found
         */
        [[nodiscard]] bool contains(const Json &val) const noexcept;

        /**
         * @brief Resize to val elements
         * @param val New size (default-inserts or truncates)
         */
        void resize(size_t val);
        /**
         * @brief Pre-allocate capacity
         * @param val Desired capacity
         */
        void reserve(size_t val);

        /**
         * @brief Iterator to first element
         * @return Mutable iterator
         */
        [[nodiscard]] Vec::iterator begin() noexcept;
        /**
         * @brief Iterator past last element
         * @return Mutable iterator
         */
        [[nodiscard]] Vec::iterator end() noexcept;
        /**
         * @brief Const iterator to first element
         * @return Const iterator
         */
        [[nodiscard]] Vec::const_iterator begin() const noexcept;
        /**
         * @brief Const iterator past last element
         * @return Const iterator
         */
        [[nodiscard]] Vec::const_iterator end() const noexcept;

        /**
         * @brief Direct access to underlying vector
         * @return Mutable reference to internal Vec
         */
        [[nodiscard]] Vec &data() noexcept { return m_data; }
        /**
         * @brief Direct access to underlying vector (const)
         * @return Const reference to internal Vec
         */
        [[nodiscard]] const Vec &data() const noexcept { return m_data; }

    public:
        /** @name Indexed access */
        /**@{*/
        /**
         * @brief Indexed access (no bounds check)
         * @param idx Element index
         * @return Reference to Json at idx
         */
        Json &operator[](size_t idx) noexcept;
        /**
         * @brief Const indexed access (no bounds check)
         * @param idx Element index
         * @return Const reference to Json at idx
         */
        const Json &operator[](size_t idx) const noexcept;

        /**
         * @brief Indexed access with bounds check
         * @param idx Element index
         * @return Reference to Json at idx
         * @throws std::out_of_range if idx >= size()
         */
        Json &at(size_t idx);
        /**
         * @brief Const indexed access with bounds check
         * @param idx Element index
         * @return Const reference to Json at idx
         * @throws std::out_of_range if idx >= size()
         */
        const Json &at(size_t idx) const;
        /**@}*/

        /**
         * @brief Append value
         * @param v Value to append (moved)
         */
        void push_back(Json v);

        /**
         * @brief Remove len elements starting at idx
         * @param idx Start index
         * @param len Number of elements to remove (default 1)
         * @throws std::out_of_range if idx or idx+len exceeds size()
         */
        void erase(size_t idx, size_t len = 1);

    public:
        /**
         * @brief Compare element-by-element
         * @param other Array to compare with
         * @return true if same size and each element equal
         */
        [[nodiscard]] bool operator==(const Array &other) const noexcept;
    };

}

#endif // INCLUDE_PJH_JSON_ARRAY_HPP
