#ifndef INCLUDE_PJH_JSON_HPP
#define INCLUDE_PJH_JSON_HPP

#include <cstdint>
#include <variant>
#include <initializer_list>
#include <fstream>
#include <xsimd/xsimd.hpp>

#include "array.hpp"
#include "object.hpp"
#include "parser.hpp"

namespace pjh::json
{

    class Json
    {

    public:
        using variant_t = std::variant<
            std::nullptr_t,
            bool,
            int64_t,
            double,
            std::string_view,
            Array,
            Object>;

    protected:
        variant_t m_data;

    public:
        Json() = default;
        Json(std::nullptr_t) : m_data(nullptr) {}
        Json(bool val) : m_data(val) {}
        Json(int64_t val) : m_data(val) {}
        Json(double val) : m_data(val) {}
        Json(std::string_view str) : m_data(str) {}
        Json(const char *str) : m_data(std::string_view(str)) {}
        Json(Array arr) : m_data(std::move(arr)) {}
        Json(Object obj) : m_data(std::move(obj)) {}
        Json(std::initializer_list<Json> vec)
            : m_data(Array(vec)) {}
        Json(std::initializer_list<Object::Entry> items)
            : m_data(Object(items)) {}

    public:
        Json(const Json &) = default;
        Json &operator=(const Json &) = default;

        Json(Json &&) noexcept = default;
        Json &operator=(Json &&) noexcept = default;

        Json &operator=(std::nullptr_t)
        {
            m_data = nullptr;
            return *this;
        }

        Json &operator=(bool val)
        {
            m_data = val;
            return *this;
        }

        Json &operator=(int64_t val)
        {
            m_data = val;
            return *this;
        }

        Json &operator=(double val)
        {
            m_data = val;
            return *this;
        }

        Json &operator=(const std::string &val)
        {
            m_data = val;
            return *this;
        }

        Json &operator=(std::string &&val) noexcept
        {
            m_data = std::move(val);
            return *this;
        }

        Json &operator=(std::string_view val)
        {
            m_data = std::string(val);
            return *this;
        }

        Json &operator=(const char *val)
        {
            m_data = std::string(val);
            return *this;
        }

        Json &operator=(const Array &arr)
        {
            m_data = arr;
            return *this;
        }

        Json &operator=(Array &&arr) noexcept
        {
            m_data = std::move(arr);
            return *this;
        }

        Json &operator=(const Object &obj)
        {
            m_data = obj;
            return *this;
        }

        Json &operator=(Object &&obj) noexcept
        {
            m_data = std::move(obj);
            return *this;
        }

    public:
        bool is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(m_data); }
        bool is_boolean() const noexcept { return std::holds_alternative<bool>(m_data); }
        bool is_int() const noexcept { return std::holds_alternative<std::int64_t>(m_data); }
        bool is_float() const noexcept { return std::holds_alternative<double>(m_data); }
        bool is_number() const noexcept { return is_int() || is_float(); }
        bool is_string() const noexcept { return std::holds_alternative<std::string_view>(m_data); }
        bool is_array() const noexcept { return std::holds_alternative<Array>(m_data); }
        bool is_object() const noexcept { return std::holds_alternative<Object>(m_data); }

    public:
        std::nullptr_t as_null() { return std::get<std::nullptr_t>(m_data); }

        bool &as_boolean() { return std::get<bool>(m_data); }
        const bool &as_boolean() const { return std::get<bool>(m_data); }

        std::int64_t &as_int() { return std::get<std::int64_t>(m_data); }
        const std::int64_t &as_int() const { return std::get<std::int64_t>(m_data); }

        double &as_float() { return std::get<double>(m_data); }
        const double &as_float() const { return std::get<double>(m_data); }

        std::string_view as_string() { return std::get<std::string_view>(m_data); }
        const std::string_view as_string() const { return std::get<std::string_view>(m_data); }

        Array &as_array() { return std::get<Array>(m_data); }
        const Array &as_array() const { return std::get<Array>(m_data); }

        Object &as_object() { return std::get<Object>(m_data); }
        const Object &as_object() const { return std::get<Object>(m_data); }

    public:
        size_t size() const noexcept
        {
            if (is_array())
                return as_array().size();
            if (is_object())
                return as_object().size();
            return 1;
        }
        bool empty() const noexcept
        {
            if (is_array())
                return as_array().empty();
            if (is_object())
                return as_object().empty();
            return !is_null();
        }

    public:
        Json &operator[](size_t idx) { return as_array()[idx]; }
        const Json &operator[](size_t idx) const { return as_array()[idx]; }

        Json &at(size_t idx)
        {
            if (!is_array())
                throw std::runtime_error("Type error: excepted array");
            return as_array().at(idx);
        }
        const Json &at(size_t idx) const
        {
            if (!is_array())
                throw std::runtime_error("Type error: excepted array");
            return as_array().at(idx);
        }

        Json &operator[](std::string_view key) { return as_object()[key]; }
        const Json &operator[](std::string_view key) const { return as_object()[key]; }

        Json &at(std::string_view key)
        {
            if (!is_object())
                throw std::runtime_error("Type error: excepted object");
            return as_object().at(key);
        }

        const Json &at(std::string_view key) const
        {
            if (!is_object())
                throw std::runtime_error("Type error: excepted object");
            return as_object().at(key);
        }

    public:
        bool operator==(const Json &other) const noexcept { return m_data == other.m_data; }
        bool operator!=(const Json &other) const noexcept { return !(this->operator==(other)); }

        bool operator==(std::nullptr_t) const noexcept { return is_null(); }
        bool operator!=(std::nullptr_t) const noexcept { return !(this->operator==(nullptr)); }

        bool operator==(bool val) const noexcept { return is_boolean() && (as_boolean() == val); }
        bool operator!=(bool val) const noexcept { return !(this->operator==(val)); }

        bool operator==(int64_t val) const noexcept { return is_int() && (as_int() == val); }
        bool operator!=(int64_t val) const noexcept { return !(this->operator==(val)); }

        bool operator==(double val) const noexcept { return is_float() && (as_float() == val); }
        bool operator!=(double val) const noexcept { return !(this->operator==(val)); }

        bool operator==(std::string_view val) const noexcept { return is_string() && (as_string() == val); }
        bool operator!=(std::string_view val) const noexcept { return !(this->operator==(val)); }

        bool operator==(const char *val) const noexcept { return operator==(std::string_view(val)); }
        bool operator!=(const char *val) const noexcept { return !(this->operator==(val)); }

        bool operator==(const Array &val) const noexcept { return is_array() && (as_array() == val); }
        bool operator!=(const Array &val) const noexcept { return !(this->operator==(val)); }

        bool operator==(const Object &val) const noexcept { return is_object() && (as_object() == val); }
        bool operator!=(const Object &val) const noexcept { return !(this->operator==(val)); }
    };

    inline Json make_null(std::nullptr_t) { return Json(nullptr); }
    inline Json make_boolean(bool val) { return Json(val); }
    inline Json make_int(int64_t val) { return Json(val); }
    inline Json make_float(double val) { return Json(val); }
    inline Json make_str(std::string str) { return Json(std::move(str)); }
    inline Json make_str(std::string_view str) { return Json(std::string(str)); }
    inline Json make_str(const char *str) { return Json(std::string(str)); }
    inline Json make_array(std::initializer_list<Json> vec) { return Json(vec); }
    inline Json make_object(std::initializer_list<Object::Entry> items) { return Json(items); }

    // ---------------------------------------------------------
    // Document Wrapper (Takes ownership of the In-Situ buffer)
    // ---------------------------------------------------------
    class Document : public Json
    {
    public:
        std::string buffer;

    public:
        Document() = default;

        Document(Json &&js, std::string &&buf)
            : Json(std::move(js)), buffer(std::move(buf)) {}

        Document(const Document &) = delete;
        Document &operator=(const Document &) = delete;
        Document(Document &&) noexcept = default;
        Document &operator=(Document &&) noexcept = default;
    };

    // ---------------------------------------------------------
    // Parser Implementation (SIMD Accelerated)
    // ---------------------------------------------------------

    inline void Parser::skip_whitespace()
    {
        using batch_type = xsimd::batch<char>;
        std::size_t batch_size = batch_type::size;
        static_assert(batch_type::size <= 64, "batch_size too large for uint64_t mask");

        auto space = xsimd::broadcast<char>(' ');
        auto tab = xsimd::broadcast<char>('\t');
        auto cr = xsimd::broadcast<char>('\r');
        auto lf = xsimd::broadcast<char>('\n');

        // 使用 SIMD 并行跳过大量空白字符
        while (true)
        {
            auto b = batch_type::load_unaligned(m_curr);
            auto is_ws = (b == space) | (b == tab) | (b == cr) | (b == lf);

            uint64_t mask = is_ws.mask();
            uint64_t non_ws_mask = ~mask;
            if constexpr (batch_type::size < 64)
            {
                non_ws_mask &= (1ULL << batch_size) - 1;
            }

            // 遇到非空白字符或遇到 EOF 的 \0 Padding，自动停下
            if (non_ws_mask != 0)
            {
                m_curr += std::countr_zero(non_ws_mask);
                return;
            }
            m_curr += batch_size;
        }

        // 处理尾部剩余数据
        while (m_curr < m_end && (*m_curr == ' ' || *m_curr == '\t' || *m_curr == '\r' || *m_curr == '\n'))
        {
            ++m_curr;
        }
    }

    inline uint32_t Parser::parse_hex4()
    {
        uint32_t code = 0;
        for (int i = 0; i < 4; ++i)
        {
            char c = *m_curr++;
            code <<= 4;
            if (c >= '0' && c <= '9')
                code |= (c - '0');
            else if (c >= 'a' && c <= 'f')
                code |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                code |= (c - 'A' + 10);
            else
                error("Invalid hex digit in unicode escape");
        }
        return code;
    }

    inline void encode_utf8(uint32_t cp, char *&dst)
    {
        if (cp <= 0x7F)
        {
            *dst++ = static_cast<char>(cp);
        }
        else if (cp <= 0x7FF)
        {
            *dst++ = static_cast<char>(0xC0 | ((cp >> 6) & 0x1F));
            *dst++ = static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp <= 0xFFFF)
        {
            *dst++ = static_cast<char>(0xE0 | ((cp >> 12) & 0x0F));
            *dst++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *dst++ = static_cast<char>(0x80 | (cp & 0x3F));
        }
        else if (cp <= 0x10FFFF)
        {
            *dst++ = static_cast<char>(0xF0 | ((cp >> 18) & 0x07));
            *dst++ = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            *dst++ = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            *dst++ = static_cast<char>(0x80 | (cp & 0x3F));
        }
        else
        {
            throw std::runtime_error("Invalid unicode codepoint");
        }
    }

    inline void handle_escape(char *&dst, const char *&m_curr, Parser *parser)
    {
        ++m_curr; // 跳过 '\\'
        switch (*m_curr)
        {
        case '"':
            *dst++ = '"';
            break;
        case '\\':
            *dst++ = '\\';
            break;
        case '/':
            *dst++ = '/';
            break;
        case 'b':
            *dst++ = '\b';
            break;
        case 'f':
            *dst++ = '\f';
            break;
        case 'n':
            *dst++ = '\n';
            break;
        case 'r':
            *dst++ = '\r';
            break;
        case 't':
            *dst++ = '\t';
            break;
        case 'u':
        {
            ++m_curr;
            uint32_t cp = parser->parse_hex4();

            if (cp >= 0xD800 && cp <= 0xDBFF)
            {
                if (m_curr[0] == '\\' && m_curr[1] == 'u')
                {
                    m_curr += 2;
                    uint32_t cp2 = parser->parse_hex4();
                    if (cp2 >= 0xDC00 && cp2 <= 0xDFFF)
                        cp = 0x10000 + (((cp - 0xD800) << 10) | (cp2 - 0xDC00));
                    else
                        throw std::runtime_error("Invalid surrogate pair");
                }
                else
                    throw std::runtime_error("Expected low surrogate");
            }
            encode_utf8(cp, dst);
            return;
        }
        default:
            throw std::runtime_error("Invalid escape character");
        }
        ++m_curr;
    }

    inline std::string_view Parser::parse_string()
    {
        if (m_curr >= m_end || *m_curr != '"')
            error("Expected '\"'");
        ++m_curr; // skip '"'

        const char *start = m_curr;
        char *dst = nullptr;

        // 使用 uint8_t 替代 char，防止把合法的 UTF-8 汉字（高位为1的负数）当成控制字符
        using batch_type = xsimd::batch<uint8_t>;
        std::size_t batch_size = batch_type::size;
        auto quote = xsimd::broadcast<uint8_t>('"');
        auto escape = xsimd::broadcast<uint8_t>('\\');

        // JSON规范：字符串中不允许出现任何 < 0x20 的控制字符（包含换行、TAB、以及 \0）
        auto ctrl = xsimd::broadcast<uint8_t>(0x20);

        // 使用 SIMD 极速推测查找字符串边界 (" 或 \)
        while (true)
        {
            auto b = batch_type::load_unaligned(
                reinterpret_cast<const uint8_t *>(m_curr));
            // 既找引号和转义符，又找非法控制字符与文件尾部的 \0 Padding
            auto matches = (b == quote) | (b == escape) | (b < ctrl);
            uint64_t mask = matches.mask();

            if constexpr (batch_type::size < 64)
                mask &= (1ULL << batch_size) - 1;

            if (mask != 0)
            {
                int skip = std::countr_zero(mask);
                m_curr += skip;

                if (*m_curr == '"')
                {
                    std::string_view res(start, m_curr - start);
                    ++m_curr;
                    return res;
                }
                else if (*m_curr == '\\')
                {
                    dst = const_cast<char *>(start) + (m_curr - start);
                    goto insitu_fallback;
                }
                else
                {
                    // 碰到了 < 0x20 的字符
                    if (m_curr >= m_end)
                        error("Unterminated string"); // 其实是碰到了 \0 Padding
                    else
                        error("Unescaped control character in string");
                }
            }
            else
                m_curr += batch_size;
        }

    insitu_fallback:
        // 原地修改循环：此时 dst 是写入指针，m_curr 是读取指针
        while (true)
        {
            if (*m_curr == '"')
            {
                ++m_curr;
                // 返回从 start 开始，到被覆写的 dst 结束的精确长度
                return std::string_view(start, dst - start);
            }
            else if (*m_curr == '\\')
            {
                handle_escape(dst, m_curr, this);
            }
            else if (static_cast<uint8_t>(*m_curr) < 0x20)
            {
                error("Unescaped control character in string");
            }
            else
            {
                *dst++ = *m_curr++;
            }
        }
    }

    inline Json Parser::parse_number()
    {
        const char *start = m_curr;
        bool is_float = false;

        if (m_curr < m_end && *m_curr == '-')
            ++m_curr;
        while (*m_curr >= '0' && *m_curr <= '9')
            ++m_curr;

        if (*m_curr == '.')
        {
            is_float = true;
            ++m_curr;
            while (*m_curr >= '0' && *m_curr <= '9')
                ++m_curr;
        }
        if (*m_curr == 'e' || *m_curr == 'E')
        {
            is_float = true;
            ++m_curr;
            if (*m_curr == '+' || *m_curr == '-')
                ++m_curr;
            while (*m_curr >= '0' && *m_curr <= '9')
                ++m_curr;
        }

        if (is_float)
        {
            double val = 0.0;
            auto [ptr, ec] = std::from_chars(start, m_curr, val);
            if (ec != std::errc() || ptr != m_curr)
                error("Invalid float format");
            return make_float(val);
        }
        else
        {
            int64_t val = 0;
            auto [ptr, ec] = std::from_chars(start, m_curr, val);
            if (ec != std::errc() || ptr != m_curr)
                error("Invalid integer format");
            return make_int(val);
        }
    }

    inline Json Parser::parse_literal()
    {
        if (m_curr[0] == 't' &&
            m_curr[1] == 'r' &&
            m_curr[2] == 'u' &&
            m_curr[3] == 'e')
        {
            m_curr += 4;
            return make_boolean(true);
        }
        if (m_curr[0] == 'f' &&
            m_curr[1] == 'a' &&
            m_curr[2] == 'l' &&
            m_curr[3] == 's' &&
            m_curr[4] == 'e')
        {
            m_curr += 5;
            return make_boolean(false);
        }
        if (m_curr[0] == 'n' &&
            m_curr[1] == 'u' &&
            m_curr[2] == 'l' &&
            m_curr[3] == 'l')
        {
            m_curr += 4;
            return make_null(nullptr);
        }
        error("Invalid literal");
    }

    inline Json Parser::parse_object()
    {
        ++m_curr; // skip '{'
        Object obj;
        skip_whitespace();
        if (*m_curr == '}')
        {
            ++m_curr;
            return Json(std::move(obj));
        }

        while (true)
        {
            skip_whitespace();
            if (*m_curr != '"')
                error("Expected string key in object");
            auto key = parse_string();

            skip_whitespace();
            if (*m_curr != ':')
                error("Expected ':' in object");
            ++m_curr;

            Json val = parse_value();
            obj.insert(key, std::move(val));

            skip_whitespace();
            if (m_curr >= m_end)
                error("Unexpected end of object");
            if (*m_curr == '}')
            {
                ++m_curr;
                return Json(std::move(obj));
            }
            if (*m_curr == ',')
                ++m_curr;
            else
                error("Expected ',' or '}' in object");
        }
    }

    inline Json Parser::parse_array()
    {
        ++m_curr; // skip '['
        Array arr;
        skip_whitespace();
        if (m_curr < m_end && *m_curr == ']')
        {
            ++m_curr;
            return Json(std::move(arr));
        }

        while (true)
        {
            arr.push_back(parse_value());

            skip_whitespace();
            if (*m_curr == ']')
            {
                ++m_curr;
                return Json(std::move(arr));
            }
            if (*m_curr == ',')
                ++m_curr;
            else
                error("Expected ',' or ']' in array");
        }
    }

    inline Json Parser::parse_value()
    {
        skip_whitespace();

        switch (*m_curr)
        {
        case '{':
            return parse_object();
        case '[':
            return parse_array();
        case '"':
            return Json(parse_string());
        case 't':
        case 'f':
        case 'n':
            return parse_literal();
        case '-':
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            return parse_number();
        case '\0':
            if (m_curr >= m_end)
                error("Unexpected end of input");
            [[fallthrough]];
        default:
            error("Unexpected character parsing value");
            error("Unexpected character parsing value");
        }
        return Json(nullptr);
    }

    inline Json Parser::parse()
    {
        Json result = parse_value();
        skip_whitespace();
        if (m_curr < m_end)
            error("Extra characters after complete JSON value");
        return result;
    }

    // ---------------------------------------------------------
    // File I/O Interface
    // ---------------------------------------------------------

    inline Document parse_file(const std::string &filepath)
    {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + filepath);
        }

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        // 将文件内容一次性读入 buffer，以便让 SIMD Parser 在连续内存上极速狂飙
        std::string buffer;
        // 在末尾安全填充 64 字节的 '\0' (SIMD Padding)
        // 保证底层的 Parser 在做无界(SIMD)读取时绝对不会越界发生段错误。
        buffer.resize(size + 64, '\0');

        if (!file.read(buffer.data(), size))
        {
            throw std::runtime_error("Failed to read file: " + filepath);
        }

        // 调用刚才写的解析器，传入准确的逻辑大小
        Parser p(std::string_view(buffer.data(), size));
        Json root = p.parse();

        // 将 DOM 与背后的数据 Buffer 打包在一起交还
        return Document(std::move(root), std::move(buffer));
    }
}

#endif // INCLUDE_PJH_JSON_HPP