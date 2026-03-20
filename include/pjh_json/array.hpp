#ifndef INCLUDE_PJH_JSON_ARRAY_HPP
#define INCLUDE_PJH_JSON_ARRAY_HPP

#include <algorithm>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <vector>

#include "json_fwd.hpp"

namespace pjh::json
{
    class Json;

    class Array
    {
    public:
        using Vec = std::pmr::vector<Json>;

    private:
        struct Impl;
        struct ImplDeleter
        {
            std::pmr::memory_resource *res{nullptr};
            void operator()(Impl *ptr) const noexcept;
        };
        std::unique_ptr<Impl, ImplDeleter> m_impl;
        std::pmr::memory_resource *m_resource{nullptr};

    public:
        Array(
            std::pmr::memory_resource *res
            = std::pmr::get_default_resource());
        Array(Vec vec);
        Array(std::initializer_list<Json> vec);

        ~Array();

        Array(const Array &);
        Array &operator=(const Array &);

        Array(Array &&) noexcept;
        Array &operator=(Array &&) noexcept;

    public:
        size_t size() const noexcept;
        bool empty() const noexcept;
        void clear() noexcept;
        bool contains(const Json &val) const noexcept;

        void resize(size_t val);
        void reserve(size_t val);

        Vec &data() noexcept;
        const Vec &data() const noexcept;

    public:
        Json &operator[](size_t idx) noexcept;
        const Json &operator[](size_t idx) const noexcept;

        Json &at(size_t idx);
        const Json &at(size_t idx) const;

        void push_back(Json v);
        void erase(size_t idx, size_t len = 1);

    public:
        bool operator==(const Array &other) const noexcept;
        bool operator!=(const Array &other) const noexcept { return !(this->operator==(other)); }
    };

}

#endif // INCLUDE_PJH_JSON_ARRAY_HPP
