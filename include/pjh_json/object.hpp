#ifndef INCLUDE_PJH_JSON_OBJECT_HPP
#define INCLUDE_PJH_JSON_OBJECT_HPP

#include <stdexcept>
#include <string>
#include <string_view>
#include <memory>
#include <ranges>
#include <memory_resource>
#include <vector>
#include <concepts>
#include <utility>

#include "config.hpp"

#include "json_fwd.hpp"

namespace pjh::json
{
    class Json;

    /**
     * @brief JSON object type (ordered map of string -> Json)
     *
     * Backed by std::pmr::vector<Entry> for insertion-order preservation.
     * Copy disabled -- use clone().
     */
    class Object
    {
    public:
        using Entry = std::pair<String, Json>;
        using Vec = std::pmr::vector<Entry>;

    private:
        Vec m_data;
        std::pmr::memory_resource *m_resource{nullptr};

    public:
        /**
         * @brief Construct empty object with given allocator
         * @param res Memory resource for entries (default: global config resource)
         */
        Object(
            std::pmr::memory_resource *res = Config::instance().resource());

        /**
         * @brief Construct from existing vector (adopts allocator)
         * @param val Pre-populated vector; allocator is inferred from val
         */
        Object(Vec val);

        ~Object() = default;

        /**
         * @brief Copy not allowed -- use clone()
         */
        Object(const Object &) = delete;
        /**
         * @brief Copy not allowed -- use clone()
         */
        Object &operator=(const Object &) = delete;

        /**
         * @brief Move construct
         * @param other Source object (left empty)
         */
        Object(Object &&) noexcept;
        /**
         * @brief Move assign
         * @param other Source object (left empty)
         * @return *this
         */
        Object &operator=(Object &&) noexcept;

        /**
         * @brief Deep copy into specified memory resource
         * @param into Allocator for copied entries (default: global config resource)
         * @return Independent deep copy
         */
        [[nodiscard]] Object clone(
            std::pmr::memory_resource *into = Config::instance().resource()) const;

        /**
         * @brief Construct object from variadic entries
         * @tparam Es Types convertible to Object::Entry
         * @param entries Key-value pairs to insert
         * @return Object containing all entries
         */
        template <class... Es>
            requires(std::convertible_to<Es, Object::Entry> && ...)
        [[nodiscard]] static Object of(Es &&...entries);

    public:
        /**
         * @brief Entry count
         * @return Number of key-value pairs
         */
        [[nodiscard]] size_t size() const noexcept;

        /**
         * @brief true if no entries
         */
        [[nodiscard]] bool empty() const noexcept;

        /**
         * @brief Remove all entries
         */
        void clear() noexcept;

        /**
         * @brief true if key exists
         * @param key Field name to search
         * @return true if key found
         */
        [[nodiscard]] bool contains(std::string_view key) const noexcept;

        /**
         * @brief Iterator to first entry
         * @return Mutable iterator over (String, Json) pairs
         */
        [[nodiscard]] Vec::iterator begin() noexcept;
        /**
         * @brief Iterator past last entry
         * @return Mutable iterator
         */
        [[nodiscard]] Vec::iterator end() noexcept;
        /**
         * @brief Const iterator to first entry
         * @return Const iterator
         */
        [[nodiscard]] Vec::const_iterator begin() const noexcept;
        /**
         * @brief Const iterator past last entry
         * @return Const iterator
         */
        [[nodiscard]] Vec::const_iterator end() const noexcept;

        /**
         * @brief Direct access to underlying vector
         * @return Mutable reference to internal Vec
         */
        [[nodiscard]] Vec &data() noexcept;
        /**
         * @brief Direct access to underlying vector (const)
         * @return Const reference to internal Vec
         */
        [[nodiscard]] const Vec &data() const noexcept;

    public:
        /** @name Field access */
        /**@{*/
        /**
         * @brief Access or insert key
         * @param key Field name
         * @return Mutable reference to Json for key
         * @note If key does not exist, default-constructed Json is inserted.
         */
        Json &operator[](std::string_view key);
        /**
         * @brief Access key (read-only)
         * @param key Field name
         * @return Const reference to Json for key
         * @throws std::out_of_range if key not found
         */
        const Json &operator[](std::string_view key) const;

        /**
         * @brief Access key with bounds check
         * @param key Field name
         * @return Mutable reference to Json for key
         * @throws std::out_of_range if key not found
         */
        Json &at(std::string_view key);
        /**
         * @brief Const access key with bounds check
         * @param key Field name
         * @return Const reference to Json for key
         * @throws std::out_of_range if key not found
         */
        const Json &at(std::string_view key) const;
        /**@}*/

    public:
        /**
         * @brief Insert or overwrite key-value pair
         * @param key Field name
         * @param val Value to assign
         */
        void insert(std::string_view key, Json val);
        /**
         * @brief Insert or overwrite entry
         * @param entry Pair of (String key, Json value)
         */
        void insert(Entry entry);

        /**
         * @brief Remove key
         * @param key Field name to remove
         * @return true if key existed and was removed
         */
        bool remove(std::string_view key);

    public:
        /**
         * @brief Compare entry-by-entry (order-sensitive)
         * @param other Object to compare with
         * @return true if same size, keys, and values in same order
         */
        [[nodiscard]] bool operator==(const Object &other) const noexcept;
    };

}

#endif // INCLUDE_PJH_JSON_OBJECT_HPP
