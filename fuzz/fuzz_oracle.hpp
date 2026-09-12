#ifndef PJH_JSON_FUZZ_ORACLE_HPP
#define PJH_JSON_FUZZ_ORACLE_HPP

// Differential oracle: pjh_json parse vs nlohmann/json v3.11.3.
// Deliberately NOT in tests/: it links nlohmann (a benchmark dep) and is only
// built in the opt-in PJH_JSON_BUILD_FUZZERS tree.

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "pjh_json/config.hpp"
#include "pjh_json/document.hpp"
#include "pjh_json/writer.hpp"

namespace pjh::json::fuzz
{
    // --- config alignment (plan 63 §3/§5.2) ------------------------------
    // nlohmann always skips a leading UTF-8 BOM and always validates raw
    // UTF-8 inside strings; enable pjh's matching opt-in policies so those
    // are not counted as divergences. strict_duplicate_keys stays OFF: both
    // implementations are last-wins.
    inline void configure_oracle() noexcept
    {
        Config::instance().set_strip_bom(true);
        Config::instance().set_strict_utf8(true);
        Config::instance().set_strict_duplicate_keys(false);
    }

    inline void fuzz_fail(const char *what) noexcept
    {
        std::fprintf(stderr, "[fuzz-oracle] divergence: %s\n", what);
        std::abort();
    }

    inline std::string_view strip_leading_bom(std::string_view s) noexcept
    {
        if (s.size() >= 3 &&
            static_cast<unsigned char>(s[0]) == 0xEF &&
            static_cast<unsigned char>(s[1]) == 0xBB &&
            static_cast<unsigned char>(s[2]) == 0xBF)
            return s.substr(3);
        return s;
    }

    // --- raw-byte scans (string-aware) -----------------------------------
    inline size_t skip_json_string(std::string_view s, size_t i) noexcept
    {
        ++i; // opening quote
        while (i < s.size())
        {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            if (c == '\\')
            {
                i += 2;
                continue;
            }
            if (c == '"')
                return i + 1;
            ++i;
        }
        return s.size();
    }

    inline size_t max_container_depth(std::string_view s) noexcept
    {
        size_t depth = 0, deepest = 0, i = 0;
        while (i < s.size())
        {
            const char c = s[i];
            if (c == '"')
            {
                i = skip_json_string(s, i);
                continue;
            }
            if (c == '[' || c == '{')
            {
                ++depth;
                if (depth > deepest)
                    deepest = depth;
            }
            else if ((c == ']' || c == '}') && depth > 0)
            {
                --depth;
            }
            ++i;
        }
        return deepest;
    }

    // True iff the stream contains a float-grammar number token that strtod
    // turns into an underflow (ERANGE, finite, |v| < DBL_MIN). pjh maps that
    // to a range ParseError; nlohmann/strtod accepts it as 0.0 and only
    // rejects overflow (isfinite == false). Documented divergence (task 61).
    inline bool has_underflow_number(std::string_view s)
    {
        size_t i = 0;
        while (i < s.size())
        {
            const char c = s[i];
            if (c == '"')
            {
                i = skip_json_string(s, i);
                continue;
            }
            if (c != '-' && !(c >= '0' && c <= '9'))
            {
                ++i;
                continue;
            }
            const size_t begin = i;
            if (s[i] == '-')
                ++i;
            size_t int_digits = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
            {
                ++i;
                ++int_digits;
            }
            bool is_float = false;
            if (i < s.size() && s[i] == '.')
            {
                is_float = true;
                ++i;
                while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                    ++i;
            }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
            {
                size_t j = i + 1;
                if (j < s.size() && (s[j] == '+' || s[j] == '-'))
                    ++j;
                size_t exp_digits = 0;
                while (j < s.size() && s[j] >= '0' && s[j] <= '9')
                {
                    ++j;
                    ++exp_digits;
                }
                if (exp_digits > 0)
                {
                    is_float = true;
                    i = j;
                }
            }
            if (int_digits == 0 || !is_float)
                continue;
            const std::string token(s.substr(begin, i - begin));
            errno = 0;
            const double v = std::strtod(token.c_str(), nullptr);
            if (errno == ERANGE && std::isfinite(v) &&
                std::fabs(v) < std::numeric_limits<double>::min())
                return true;
        }
        return false;
    }

    // nlohmann's lexer unconditionally maps a raw NUL byte (0x00) to
    // end_of_input, wherever it occurs (lexer.hpp get(): "the null byte is
    // needed when parsing from string literals"). A stream with an embedded
    // NUL is therefore truncated on the reference side, while pjh correctly
    // rejects the NUL as an invalid character. The reference cannot judge
    // bytes after the NUL, so any such input is a documented divergence.
    inline bool has_embedded_nul(std::string_view s) noexcept
    {
        return s.find('\0') != std::string_view::npos;
    }

    // --- value normalization ---------------------------------------------
    // Collapse every number to double so the documented int64/uint64/double
    // representation difference cannot produce a false mismatch. Exactness
    // is separately pinned by pjh's own dump->parse->== round-trip.
    inline nlohmann::json normalize_numbers(const nlohmann::json &j)
    {
        if (j.is_number())
            return nlohmann::json(j.get<double>());
        if (j.is_array())
        {
            nlohmann::json out = nlohmann::json::array();
            for (const auto &e : j)
                out.push_back(normalize_numbers(e));
            return out;
        }
        if (j.is_object())
        {
            nlohmann::json out = nlohmann::json::object();
            for (auto it = j.begin(); it != j.end(); ++it)
                out[it.key()] = normalize_numbers(it.value());
            return out;
        }
        return j;
    }

    inline bool nlohmann_accepts(std::string_view in) noexcept
    {
        try
        {
            (void)nlohmann::json::parse(in.begin(), in.end());
            return true;
        }
        catch (const std::exception &)
        {
            return false;
        }
    }

    // --- parse_copy oracle -----------------------------------------------
    inline bool oracle_parse_copy(std::string_view in,
                                  bool allow_divergences = true)
    {
        configure_oracle();

        std::unique_ptr<Document> doc;
        bool pjh_ok = true;
        try
        {
            doc = std::make_unique<Document>(parse_copy(in));
        }
        catch (const ParseError &)
        {
            pjh_ok = false;
        }
        // Any non-ParseError escaping the parse core is a hard failure, not a
        // rejection: let it propagate to libFuzzer (std::bad_alloc etc.).

        const bool nl_ok = nlohmann_accepts(in);

        if (!pjh_ok && nl_ok)
        {
            if (allow_divergences &&
                (has_underflow_number(in) ||
                 max_container_depth(in) > Config::kDefaultMaxDepth ||
                 has_embedded_nul(in)))
                return true;
            return false;
        }
        if (pjh_ok && !nl_ok)
            return false;
        if (!pjh_ok)
            return true;

        // Accepted by both: exact pjh round-trip ...
        std::pmr::string dumped = dump(doc->root(), {}, doc->resource());
        const std::string_view dv(dumped.data(), dumped.size());
        try
        {
            Document re = parse_copy(dv);
            if (!(re.root() == doc->root()))
                return false;
        }
        catch (const std::exception &)
        {
            return false;
        }

        // ... and nlohmann-semantic agreement (numbers normalized to double).
        try
        {
            const auto a = normalize_numbers(
                nlohmann::json::parse(in.begin(), in.end()));
            const auto b = normalize_numbers(
                nlohmann::json::parse(dv.begin(), dv.end()));
            if (a != b)
                return false;
        }
        catch (const std::exception &)
        {
            return false;
        }
        return true;
    }

    // --- parse_jsonl oracle ----------------------------------------------
    // Mirror parse.cpp's line rules exactly (LF split, one trailing CR
    // stripped, skip lines only of {space,tab,CR}).
    inline std::vector<std::string_view> jsonl_lines(std::string_view in) noexcept
    {
        std::vector<std::string_view> out;
        const size_t n = in.size();
        size_t i = 0;
        while (i < n)
        {
            size_t nl = i;
            while (nl < n && in[nl] != '\n')
                ++nl;
            size_t len = nl - i;
            if (len > 0 && in[i + len - 1] == '\r')
                --len;
            bool blank = true;
            for (size_t k = 0; k < len; ++k)
            {
                const char c = in[i + k];
                if (c != ' ' && c != '\t' && c != '\r')
                {
                    blank = false;
                    break;
                }
            }
            if (!blank)
                out.push_back(in.substr(i, len));
            i = (nl < n) ? nl + 1 : n;
        }
        return out;
    }

    inline bool oracle_parse_jsonl(std::string_view raw,
                                   bool allow_divergences = true)
    {
        configure_oracle();

        // pjh consumes a whole-input BOM once under strip_bom; mirror that in
        // the nlohmann reference. A BOM at a later line start is a syntax
        // error for pjh, so the reference must treat it as rejected too
        // (nlohmann would otherwise skip it per document).
        const std::string_view in = strip_leading_bom(raw);
        const auto lines = jsonl_lines(in);

        bool ref_ok = true;
        nlohmann::json ref = nlohmann::json::array();
        for (const auto line : lines)
        {
            if (line.size() >= 3 &&
                static_cast<unsigned char>(line[0]) == 0xEF &&
                static_cast<unsigned char>(line[1]) == 0xBB &&
                static_cast<unsigned char>(line[2]) == 0xBF)
            {
                ref_ok = false;
                break;
            }
            try
            {
                ref.push_back(nlohmann::json::parse(line.begin(), line.end()));
            }
            catch (const std::exception &)
            {
                ref_ok = false;
                break;
            }
        }

        std::unique_ptr<Document> doc;
        bool pjh_ok = true;
        try
        {
            doc = std::make_unique<Document>(parse_jsonl(raw));
        }
        catch (const ParseError &)
        {
            pjh_ok = false;
        }

        if (!pjh_ok && ref_ok)
        {
            if (allow_divergences &&
                (has_underflow_number(in) ||
                 max_container_depth(in) > Config::kDefaultMaxDepth ||
                 has_embedded_nul(in)))
                return true;
            return false;
        }
        if (pjh_ok && !ref_ok)
            return false;
        if (!pjh_ok)
            return true;

        std::pmr::string dumped = dump(doc->root(), {}, doc->resource());
        const std::string_view dv(dumped.data(), dumped.size());
        try
        {
            const auto got = normalize_numbers(
                nlohmann::json::parse(dv.begin(), dv.end()));
            if (normalize_numbers(ref) != got)
                return false;
        }
        catch (const std::exception &)
        {
            return false;
        }
        return true;
    }

    // --- self-test (called from every LLVMFuzzerInitialize) ---------------
    // Proves both directions: agreements hold, and the divergence detector
    // actually fires (canary with the allowlist disabled). A stubbed oracle
    // that always returns true would fail here.
    inline void run_oracle_selftest()
    {
        auto expect = [](bool cond, const char *msg) {
            if (!cond)
            {
                std::fprintf(stderr, "[fuzz-oracle] selftest FAIL: %s\n", msg);
                std::abort();
            }
        };

        // Agreements (allowlist irrelevant).
        expect(oracle_parse_copy(R"({"a":1,"a":2})"), "dup keys must agree (last-wins)");
        expect(oracle_parse_copy(R"([1,2,3])"), "array must agree");
        expect(oracle_parse_copy(R"([1,2)"), "truncated must agree (both reject)");
        expect(oracle_parse_copy(R"({"a":1} trailing)"), "trailing garbage must agree");
        expect(oracle_parse_copy("\xEF\xBB\xBF{\"a\":1}"), "BOM aligned by strip_bom");

        // Raw ill-formed UTF-8 inside a string: nlohmann always rejects, and
        // strict_utf8(true) makes pjh reject too. Byte 0xFF is pushed
        // explicitly so the source literal stays encoding-independent.
        std::string bad_utf8 = "\"[";
        bad_utf8.push_back(static_cast<char>(0xFF));
        bad_utf8 += "]\"";
        expect(oracle_parse_copy(bad_utf8), "bad UTF-8 aligned by strict_utf8");

        // Documented divergence: allowlisted, and detected when disabled.
        expect(oracle_parse_copy("1e-999"), "underflow must be allowlisted");
        expect(!oracle_parse_copy("1e-999", /*allow_divergences=*/false),
               "canary: underflow must be reported without the allowlist");

        // Embedded NUL: nlohmann truncates at 0x00, pjh rejects the byte.
        std::string with_nul = "1";
        with_nul.push_back('\0');
        expect(oracle_parse_copy(with_nul), "embedded NUL must be allowlisted");
        expect(!oracle_parse_copy(with_nul, /*allow_divergences=*/false),
               "canary: embedded NUL reported without the allowlist");

        // Depth divergence: allowlisted, and detected when disabled.
        std::string deep(600, '[');
        deep += std::string(600, ']');
        expect(oracle_parse_copy(deep), "depth > 512 must be allowlisted");
        expect(!oracle_parse_copy(deep, /*allow_divergences=*/false),
               "canary: depth divergence must be reported without the allowlist");

        // JSONL reference.
        expect(oracle_parse_jsonl("1\n2\n3\n"), "jsonl lines must agree");
        expect(oracle_parse_jsonl("\xEF\xBB\xBF" "1\n2\n"), "jsonl whole-input BOM");
        expect(oracle_parse_jsonl("1\n" "\xEF\xBB\xBF" "2\n"), "jsonl later-line BOM rejected");
    }
}

#endif // PJH_JSON_FUZZ_ORACLE_HPP
