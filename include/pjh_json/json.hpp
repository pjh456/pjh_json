#ifndef INCLUDE_PJH_JSON_HPP
#define INCLUDE_PJH_JSON_HPP

#include <cstdint>
#include <variant>
#include <initializer_list>
#include <fstream>
#include <memory_resource>
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

        Json &operator=(std::string_view val)
        {
            m_data = val;
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
    inline Json make_str(std::string_view str) { return Json(str); }
    inline Json make_str(const char *str) { return Json(str); }
    inline Json make_array(std::initializer_list<Json> vec) { return Json(vec); }
    inline Json make_object(std::initializer_list<Object::Entry> items) { return Json(items); }

    // ---------------------------------------------------------
    // Document Wrapper (Takes ownership of the In-Situ buffer)
    // ---------------------------------------------------------
    class Document : public Json
    {
    public:
        std::pmr::string buffer;

    public:
        Document() = default;

        Document(Json &&js, std::pmr::string &&buf)
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
        // 任何合法的 JSON 结构字符（双引号, 括号, 字母, 数字）ASCII 全都大于 0x20，
        // 只有 4 个空白符（空格:32, 回车:13, 换行:10, Tab:9）是 <= 0x20 的。
        if (static_cast<uint8_t>(*m_curr) > 0x20)
            return;

        using batch_type = xsimd::batch<uint8_t>;
        std::size_t batch_size = batch_type::size;
        static_assert(
            batch_type::size <= 64,
            "batch_size too large for uint64_t mask");

        auto ctrl_space = xsimd::broadcast<uint8_t>(0x20);
        auto zero = xsimd::broadcast<uint8_t>(0);

        // 使用 SIMD 并行跳过大量空白字符
        while (true)
        {
            auto b = batch_type::load_unaligned(
                reinterpret_cast<const uint8_t *>(m_curr));
            auto is_ws = (b <= ctrl_space) & (b != zero);

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
        bool is_negative = false;

        if (*m_curr == '-')
        {
            is_negative = true;
            ++m_curr;
        }

        uint64_t uval = 0;
        uint32_t digits = 0;

        // 快路径：一遍循环直接算出整数，彻底跳过 from_chars 的初始化开销！
        while (*m_curr >= '0' && *m_curr <= '9')
        {
            uval = uval * 10 + (*m_curr - '0');
            ++m_curr;
            ++digits;
        }

        // 如果碰到了小数点/指数，或者整数过长（防止 uint64_t 溢出），安全降级给 from_chars
        if (*m_curr == '.' || *m_curr == 'e' || *m_curr == 'E' || digits > 18)
        {
            if (*m_curr == '.')
            {
                ++m_curr;
                while (*m_curr >= '0' && *m_curr <= '9')
                    ++m_curr;
            }
            if (*m_curr == 'e' || *m_curr == 'E')
            {
                ++m_curr;
                if (*m_curr == '+' || *m_curr == '-')
                    ++m_curr;
                while (*m_curr >= '0' && *m_curr <= '9')
                    ++m_curr;
            }
            double val = 0.0;
            std::from_chars(start, m_curr, val);
            return make_float(val);
        }

        int64_t val = is_negative ? -static_cast<int64_t>(uval) : static_cast<int64_t>(uval);
        return make_int(val);
    }

    inline Json Parser::parse_literal()
    {
        // 借助 C++20 std::bit_cast，在编译期动态生成完美贴合当前架构端序的 Magic Number 和 Mask
        struct UChar4
        {
            uint8_t c[4];
        };
        constexpr uint32_t true_magic = std::bit_cast<uint32_t>(UChar4{'t', 'r', 'u', 'e'});
        constexpr uint32_t null_magic = std::bit_cast<uint32_t>(UChar4{'n', 'u', 'l', 'l'});

        struct UChar8
        {
            uint8_t c[8];
        };
        constexpr uint64_t false_magic = std::bit_cast<uint64_t>(UChar8{'f', 'a', 'l', 's', 'e', 0, 0, 0});
        constexpr uint64_t false_mask = std::bit_cast<uint64_t>(UChar8{0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0});

        // 借助末尾的安全 Padding，直接强读 4/8 字节，将四五次字符比较压缩成 1 次 CPU 寄存器比较
        uint32_t val32;
        std::memcpy(&val32, m_curr, 4);

        if (val32 == true_magic)
        {
            m_curr += 4;
            return make_boolean(true);
        }
        if (val32 == null_magic)
        {
            m_curr += 4;
            return make_null(nullptr);
        }

        uint64_t val64;
        std::memcpy(&val64, m_curr, 8);
        // 使用端序安全的掩码消除后续填充字符的干扰
        if ((val64 & false_mask) == false_magic)
        {
            m_curr += 5;
            return make_boolean(false);
        }

        error("Invalid literal");
    }

    inline Json Parser::parse_object()
    {
        ++m_curr; // skip '{'
        Object obj(m_resource);
        skip_whitespace();
        if (*m_curr == '}')
        {
            ++m_curr;
            return Json(std::move(obj));
        }

        // 避免 Object 的早期扩容
        obj.data().reserve(4);

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

            obj.data().emplace_back(key, Json(nullptr));
            parse_value_inplace(obj.data().back().second);

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
        Array arr(m_resource);
        skip_whitespace();
        if (m_curr < m_end && *m_curr == ']')
        {
            ++m_curr;
            return Json(std::move(arr));
        }

        // 避免 Array 的早期扩容
        arr.data().reserve(4);

        while (true)
        {
            // 先在底层数组里占一个坑位（默认构造一个 null 的 Json）
            arr.data().emplace_back(nullptr);

            // 把最后这个坑位的引用，直接传给 parse_value 去覆盖写
            parse_value_inplace(arr.data().back());

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

    inline void Parser::parse_object_inplace(Json &out)
    {
        ++m_curr; // skip '{'
        Object obj(m_resource);
        skip_whitespace();
        if (*m_curr == '}')
        {
            ++m_curr;
            out = std::move(obj);
            return;
        }

        obj.data().reserve(4);

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

            obj.data().emplace_back(key, Json(nullptr));
            parse_value_inplace(obj.data().back().second);

            skip_whitespace();
            if (m_curr >= m_end)
                error("Unexpected end of object");
            if (*m_curr == '}')
            {
                ++m_curr;
                out = std::move(obj);
                return;
            }
            if (*m_curr == ',')
                ++m_curr;
            else
                error("Expected ',' or '}' in object");
        }
    }

    inline void Parser::parse_array_inplace(Json &out)
    {
        ++m_curr; // skip '['
        Array arr(m_resource);
        skip_whitespace();
        if (m_curr < m_end && *m_curr == ']')
        {
            ++m_curr;
            out = std::move(arr);
            return;
        }

        arr.data().reserve(4);

        while (true)
        {
            arr.data().emplace_back(nullptr);
            parse_value_inplace(arr.data().back());

            skip_whitespace();
            if (*m_curr == ']')
            {
                ++m_curr;
                out = std::move(arr);
                return;
            }
            if (*m_curr == ',')
                ++m_curr;
            else
                error("Expected ',' or ']' in array");
        }
    }

    inline void Parser::parse_value_inplace(Json &out)
    {
        skip_whitespace();
        switch (*m_curr)
        {
        case '{':
            parse_object_inplace(out);
            return;
        case '[':
            parse_array_inplace(out);
            return;
        case '"':
            out = parse_string();
            return;
        case 't':
        case 'f':
        case 'n':
            out = parse_literal();
            return;
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
            out = parse_number();
            return;
        default:
            error("Unexpected character");
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

    inline Document parse_file(
        const std::string &filepath,
        std::pmr::memory_resource *res =
            std::pmr::get_default_resource())
    {
        std::ifstream file(filepath, std::ios::binary | std::ios::ate);
        if (!file.is_open())
            throw std::runtime_error("Failed to open file: " + filepath);

        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        // 将文件内容一次性读入 buffer，以便让 SIMD Parser 在连续内存上极速狂飙
        std::pmr::string buffer(res);
        // 在末尾安全填充 64 字节的 '\0' (SIMD Padding)
        // 保证底层的 Parser 在做无界(SIMD)读取时绝对不会越界发生段错误。
        buffer.resize(size + 64, '\0');

        if (!file.read(buffer.data(), size))
            throw std::runtime_error("Failed to read file: " + filepath);

        // 调用刚才写的解析器，传入准确的逻辑大小
        Parser p(std::string_view(buffer.data(), size), res);
        Json root = p.parse();

        // 将 DOM 与背后的数据 Buffer 打包在一起交还
        return Document(std::move(root), std::move(buffer));
    }
}

#endif // INCLUDE_PJH_JSON_HPP
