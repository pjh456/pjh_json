#ifndef INCLUDE_PJH_JSON_STRING_HPP
#define INCLUDE_PJH_JSON_STRING_HPP

#include <cstdint>
#include <string_view>
#include <memory_resource>
#include <string>

namespace pjh::json
{
    /**
     * @brief JSON string: view (borrowed) or heap-allocated (owned)
     *
     * Replaces std::variant<string_view, pmr::string> with a manual tagged
     * union to reduce size from 40 to 16 bytes. Borrowed strings store a
     * {ptr, len} pair inline. Owned strings store a pointer to a
     * heap-allocated pmr::string. The m_owned flag selects the active member.
     *
     * Move-only -- copy is deleted.
     */
    class String
    {
        bool m_owned = false;

        struct ViewData
        {
            const char *data;
            uint32_t length;
        };

        union
        {
            ViewData m_view;           // valid when !m_owned
            std::pmr::string *m_heap;  // valid when m_owned
        };

    public:
        /**
         * @brief Construct empty (null view)
         */
        String() noexcept : m_view{nullptr, 0} {}

        /**
         * @brief Borrowed view from string_view (no copy)
         * @param sv Source view -- caller must guarantee lifetime
         * @note Stores {ptr, len} inline. Does NOT copy.
         */
        String(std::string_view sv) noexcept : m_owned(false)
        {
            m_view.data = sv.data();
            m_view.length = static_cast<uint32_t>(sv.size());
        }

        /**
         * @brief Borrowed view from C string (no copy)
         * @param s NUL-terminated source -- caller must guarantee lifetime
         * @note Wraps s in string_view. Does NOT copy.
         */
        String(const char *s) noexcept : m_owned(false)
        {
            std::string_view sv(s);
            m_view.data = sv.data();
            m_view.length = static_cast<uint32_t>(sv.size());
        }

        /**
         * @brief Owned string (takes ownership of heap-allocated pmr::string)
         * @param s Pointer to heap-allocated pmr::string.
         * @note Caller transfers ownership. Destructor will delete.
         */
        String(std::pmr::string *s) noexcept : m_owned(true), m_heap(s) {}

        /**
         * @brief Destructor -- deletes owned pmr::string if present
         */
        ~String()
        {
            if (m_owned)
            {
                delete m_heap;
                m_owned = false;
            }
        }

        /**
         * @brief Copy not allowed -- use own() to materialise
         */
        String(const String &) = delete;
        /**
         * @brief Copy not allowed -- use own() to materialise
         */
        String &operator=(const String &) = delete;

        /**
         * @brief Move construct (steals heap pointer, marks source empty)
         * @param other Source (left as empty view)
         */
        String(String &&other) noexcept : m_owned(other.m_owned)
        {
            if (other.m_owned)
                m_heap = other.m_heap;
            else
                m_view = other.m_view;
            other.m_owned = false;
        }

        /**
         * @brief Move assign (destroys old value, steals from source)
         * @param other Source (left as empty view)
         * @return *this
         */
        String &operator=(String &&other) noexcept
        {
            if (this != &other)
            {
                this->~String();
                m_owned = other.m_owned;
                if (other.m_owned)
                    m_heap = other.m_heap;
                else
                    m_view = other.m_view;
                other.m_owned = false;
            }
            return *this;
        }

        /**
         * @brief Implicit conversion to string_view
         * @return View of content (works for both borrowed and owned)
         */
        [[nodiscard]] operator std::string_view() const noexcept
        {
            if (m_owned)
                return *m_heap;
            return std::string_view(m_view.data, m_view.length);
        }

        /**
         * @brief true if data is owned (pmr::string on heap), not borrowed
         */
        [[nodiscard]] bool is_owned() const noexcept { return m_owned; }

        /**
         * @brief Materialise borrowed view as owned copy if not already owned
         *
         * 1. If already owned: no-op.
         * 2. If borrowed: allocate a new pmr::string from the given resource,
         *    copy the view content into it, store the pointer.
         *
         * @param res Memory resource for the copy (default: default_resource)
         * @note Safe to call multiple times; subsequent calls are no-ops.
         */
        void own(std::pmr::memory_resource *res = std::pmr::get_default_resource())
        {
            if (!m_owned)
            {
                auto sv = std::string_view(m_view.data, m_view.length);
                m_heap = new std::pmr::string(sv, res);
                m_owned = true;
            }
        }

        /**
         * @brief Release ownership of the internal pmr::string.
         *
         * After this call, the String becomes an empty view. The caller
         * takes ownership of the returned pointer and must delete it.
         *
         * @return Pointer to owned pmr::string, or nullptr if was borrowed.
         */
        [[nodiscard]] std::pmr::string *release() noexcept
        {
            if (m_owned)
            {
                auto *p = m_heap;
                m_owned = false;
                return p;
            }
            return nullptr;
        }

        /**
         * @brief Compare with another String (content only, not storage)
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
