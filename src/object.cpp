#include "pjh_json/object.hpp"
#include "pjh_json/json.hpp"

#include <ranges>

namespace pjh::json
{
    bool Object::contains(const Json &val) const noexcept
    {
        return std::ranges::find_if(
                   m_data,
                   [&](const auto &kv)
                   { return kv.second == val; }) !=
               m_data.end();
    }

    Json &Object::operator[](std::string_view key)
    {
        auto it = std::find_if(
            m_data.begin(), m_data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_data.end())
        {
            m_data.emplace_back(key, Json());
            return m_data.back().second;
        }

        return it->second;
    }

    const Json &Object::operator[](std::string_view key) const
    {
        auto it = std::ranges::find_if(
            m_data.begin(), m_data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_data.end())
            throw std::out_of_range("json key not found");

        return it->second;
    }

    Json &Object::at(std::string_view key)
    {
        auto it = std::ranges::find_if(
            m_data.begin(), m_data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_data.end())
            throw std::out_of_range("json key not found");

        return it->second;
    }

    const Json &Object::at(std::string_view key) const
    {
        auto it = std::ranges::find_if(
            m_data.begin(), m_data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_data.end())
            throw std::out_of_range("json key not found");

        return it->second;
    }

    void Object::insert(std::string_view key, Json val)
    {
        insert(std::make_pair(key, val));
    }

    void Object::insert(Entry entry)
    {
        m_data.push_back(std::move(entry));
    }

    void Object::remove(std::string_view key)
    {
        auto it = std::ranges::find_if(
            m_data.begin(), m_data.end(),
            [&](auto &kv)
            { return kv.first == key; });

        if (it == m_data.end())
            return;

        m_data.erase(it);
    }

    bool Object::operator==(const Object &other) const noexcept
    {
        return std::equal(
            m_data.begin(),
            m_data.end(),
            other.m_data.begin(),
            [](const Entry &x, const Entry &y)
            { return x.first == y.first && x.second == y.second; });
    }
}
