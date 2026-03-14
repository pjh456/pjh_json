#pragma once

#include <vector>
#include <algorithm>

#include "json_fwd.hpp"

namespace pjh::json
{
    class Json;

    class Array
    {
    public:
        using Vec = std::vector<Json>;

    private:
        Vec m_data;

    public:
        Array() = default;
        Array(Vec vec) : m_data(std::move(vec)) {}

        ~Array() = default;

        Array(const Array &) = default;
        Array &operator=(const Array &) = default;

        Array(Array &&) noexcept = default;
        Array &operator=(Array &&) noexcept = default;

    public:
        size_t size() const noexcept { return m_data.size(); }
        bool empty() const noexcept { return m_data.empty(); }
        void clear() noexcept { return m_data.clear(); }
        bool contains(const Json &val) const noexcept
        {
            return std::find(
                       m_data.begin(),
                       m_data.end(),
                       val) !=
                   m_data.end();
        }

        Vec &data() noexcept { return m_data; }
        const Vec &data() const noexcept { return m_data; }

    public:
        Json &operator[](size_t idx) noexcept { return m_data[idx]; }
        const Json &operator[](size_t idx) const noexcept { return m_data[idx]; }

        Json &at(size_t idx) { return m_data.at(idx); }
        const Json &at(size_t idx) const { return m_data.at(idx); }

        void push_back(Json v);
        void erase(size_t idx, size_t len = 1) { m_data.erase(m_data.begin() + idx, m_data.begin() + idx + len); }

    public:
        bool operator==(const Array &other) const noexcept;
        bool operator!=(const Array &other) const noexcept { return !(this->operator==(other)); }
    };

}