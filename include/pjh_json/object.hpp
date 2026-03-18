#ifndef INCLUDE_PJH_JSON_OBJECT_HPP
#define INCLUDE_PJH_JSON_OBJECT_HPP

#include <stdexcept>
#include <vector>
#include <string>
#include <ranges>
#include <memory_resource>

#include "json_fwd.hpp"

namespace pjh::json
{
    class Json;

    class Object
    {
    public:
        using string_type = std::pmr::string; 
        using Entry = std::pair<string_type, Json>;
        using Vec = std::pmr::vector<Entry>;

    private:
        Vec m_data;

    public:
        Object(
            std::pmr::memory_resource* res 
            = std::pmr::get_default_resource()) 
            : m_data(res) {}
        Object(Vec val) : m_data(std::move(val)) {}
        Object(std::initializer_list<Entry> items) : m_data(items) {}

        ~Object() = default;

        Object(const Object &) = default;
        Object &operator=(const Object &) = default;

        Object(Object &&) noexcept = default;
        Object &operator=(Object &&) noexcept = default;

    public:
        size_t size() const noexcept { return m_data.size(); }
        bool empty() const noexcept { return m_data.empty(); }
        void clear() noexcept { m_data.clear(); }
        bool contains(const Json &val) const noexcept;

        Vec &data() noexcept { return m_data; }
        const Vec &data() const noexcept { return m_data; }

    public:
        Json &operator[](std::string_view key);
        const Json &operator[](std::string_view key) const;

        Json &at(std::string_view key);
        const Json &at(std::string_view key) const;

    public:
        void insert(std::string_view key, Json val);
        void insert(Entry entry);

        void remove(std::string_view key);

    public:
        bool operator==(const Object &other) const noexcept;
        bool operator!=(const Object &other) const noexcept { return !(this->operator==(other)); }
    };

}

#endif // INCLUDE_PJH_JSON_OBJECT_HPP