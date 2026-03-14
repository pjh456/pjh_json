#ifndef INCLUDE_PJH_JSON_OBJECT_HPP
#define INCLUDE_PJH_JSON_OBJECT_HPP

#include <stdexcept>
#include <vector>
#include <string>
#include <ranges>

#include "json_fwd.hpp"

namespace pjh::json
{
    class Json;

    class Object
    {
    public:
        using Entry = std::pair<std::string, Json>;
        using Vec = std::vector<Entry>;

    private:
        Vec m_data;

    public:
        Object() = default;
        Object(Vec val) : m_data(std::move(val)) {}

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