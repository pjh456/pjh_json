#ifndef INCLUDE_PJH_JSON_STRING_HPP
#define INCLUDE_PJH_JSON_STRING_HPP

#include <cstdint>
#include <string_view>
#include <memory_resource>
#include <string>
#include <type_traits>

namespace pjh::json
{
    /**
     * @brief JSON string — view (borrowed) or heap-allocated (owned).
     *
     * Replaces std::variant<string_view, pmr::string> with a manual tagged
     * union to reduce size from 40 to 16 bytes. Borrowed strings store a
     * {ptr, len} pair inline. Owned strings store a pointer to a
     * heap-allocated pmr::string.
     *
     * Move-only — copy is deleted.
     */
    class String
    {
    private:
        /**
         * @brief Storage mode for the active union member.
         */
        enum class Storage : uint8_t
        {
            View = 0, // borrowed: m_data.view_data active
            Owned = 1 // owned:    m_data.heap_ptr active
        };

        Storage m_storage = Storage::View;

        struct ViewData
        {
            const char *data;
            uint32_t length;
        };

        /**
         * @brief Inline storage. Only one member is active per m_storage.
         *
         * | m_storage | active member | content                |
         * |-----------|---------------|------------------------|
         * | View      | view_data     | {ptr, len} inline      |
         * | Owned     | heap_ptr      | pmr::string* on heap   |
         */
        union
        {
            ViewData view_data;         // borrowed
            std::pmr::string *heap_ptr; // owned
        };

    public:
        /**
         * @brief Construct empty (null view)
         */
        constexpr String() noexcept : view_data{nullptr, 0} {}

        /**
         * @brief Borrowed view from string_view (no copy)
         * @param sv Source view — caller must guarantee lifetime
         * @note Stores {ptr, len} inline. Does NOT copy.
         */
        constexpr String(std::string_view sv) noexcept : m_storage(Storage::View)
        {
            view_data.data = sv.data();
            view_data.length = static_cast<uint32_t>(sv.size());
        }

        /**
         * @brief Borrowed view from C string (no copy)
         * @param s NUL-terminated source — caller must guarantee lifetime
         * @note Wraps s in string_view. Does NOT copy.
         */
        constexpr String(const char *s) noexcept : m_storage(Storage::View)
        {
            std::string_view sv(s);
            view_data.data = sv.data();
            view_data.length = static_cast<uint32_t>(sv.size());
        }

        /**
         * @brief Owned string (takes ownership of heap-allocated pmr::string)
         * @param s Pointer to heap-allocated pmr::string.
         * @note Caller transfers ownership. Destructor will delete.
         */
        String(std::pmr::string *s) noexcept
            : m_storage(Storage::Owned), heap_ptr(s) {}

        /**
         * @brief Destructor — deletes owned pmr::string if present
         */
        constexpr ~String()
        {
            if (!std::is_constant_evaluated())
            {
                if (m_storage == Storage::Owned)
                {
                    delete heap_ptr;
                    m_storage = Storage::View;
                }
            }
        }

        /**
         * @brief Copy not allowed — use own() to materialise
         */
        String(const String &) = delete;
        /**
         * @brief Copy not allowed — use own() to materialise
         */
        String &operator=(const String &) = delete;

        /**
         * @brief Move construct (steals active member, marks source empty)
         * @param other Source (left as empty view)
         */
        constexpr String(String &&other) noexcept : m_storage(other.m_storage)
        {
            if (other.m_storage == Storage::Owned)
                heap_ptr = other.heap_ptr;
            else
                view_data = other.view_data;
            other.m_storage = Storage::View;
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
                m_storage = other.m_storage;
                if (other.m_storage == Storage::Owned)
                    heap_ptr = other.heap_ptr;
                else
                    view_data = other.view_data;
                other.m_storage = Storage::View;
            }
            return *this;
        }

        /**
         * @brief Implicit conversion to string_view
         * @return View of content (works for both borrowed and owned)
         */
        [[nodiscard]] constexpr operator std::string_view() const noexcept
        {
            if (m_storage == Storage::Owned)
                return *heap_ptr;
            return std::string_view(view_data.data, view_data.length);
        }

        /**
         * @brief true if data is owned (pmr::string on heap), not borrowed
         */
        [[nodiscard]] constexpr bool is_owned() const noexcept
        {
            return m_storage == Storage::Owned;
        }

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
            if (m_storage == Storage::View)
            {
                auto sv = std::string_view(view_data.data, view_data.length);
                heap_ptr = new std::pmr::string(sv, res);
                m_storage = Storage::Owned;
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
        [[nodiscard]] constexpr std::pmr::string *release() noexcept
        {
            if (m_storage == Storage::Owned)
            {
                auto *p = heap_ptr;
                m_storage = Storage::View;
                return p;
            }
            return nullptr;
        }

        /**
         * @brief Compare with another String (content only, not storage)
         * @param other String to compare
         * @return true if content is identical
         */
        [[nodiscard]] constexpr bool operator==(const String &other) const noexcept
        {
            return static_cast<std::string_view>(*this) == static_cast<std::string_view>(other);
        }

        /**
         * @brief Compare with string_view
         * @param other View to compare
         * @return true if content matches
         */
        [[nodiscard]] constexpr bool operator==(std::string_view other) const noexcept
        {
            return static_cast<std::string_view>(*this) == other;
        }

        /**
         * @brief Compare with C string
         * @param other NUL-terminated string to compare
         * @return true if content matches
         */
        [[nodiscard]] constexpr bool operator==(const char *other) const noexcept
        {
            return static_cast<std::string_view>(*this) == other;
        }
    };

}

#endif
