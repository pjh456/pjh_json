#ifndef INCLUDE_PJH_JSON_WRITER_HPP
#define INCLUDE_PJH_JSON_WRITER_HPP

#include <cstdint>
#include <string_view>
#include <memory_resource>
#include <iosfwd>

#include "json.hpp"

namespace pjh::json
{
    struct DumpOptions
    {
        bool pretty = false;      // false = compact
        uint8_t indent = 2;       // spaces per level when pretty
        char indent_char = ' ';   // ' ' or '\t'
        bool ascii = false;       // true = escape non-ASCII as \uXXXX
        bool sort_keys = false;   // true = emit object keys sorted
    };

    // ---- value dump ----
    [[nodiscard]] std::pmr::string dump(
        const Json &value,
        const DumpOptions &opts = {},
        std::pmr::memory_resource *res = Config::instance().resource());

    [[nodiscard]] std::pmr::string dump(
        const Document &doc,
        const DumpOptions &opts = {},
        std::pmr::memory_resource *res = Config::instance().resource());

    void dump_to(std::pmr::string &sink, const Json &value, const DumpOptions &opts = {});
    void dump_to(std::ostream &os, const Json &value, const DumpOptions &opts = {});
    void dump_file(std::string_view path, const Json &value, const DumpOptions &opts = {});

    // ---- jsonl (root is an Array, one element per line, always compact) ----
    [[nodiscard]] std::pmr::string dump_jsonl(
        const Array &arr,
        std::pmr::memory_resource *res = Config::instance().resource());

    void dump_jsonl_to(std::pmr::string &sink, const Array &arr);
    void dump_jsonl_file(std::string_view path, const Array &arr);

    // ---- prettifier = parse + dump(pretty) ----
    [[nodiscard]] std::pmr::string prettify(
        std::string_view json,
        uint8_t indent = 2,
        std::pmr::memory_resource *res = Config::instance().resource());

    // ---- internal (shared by writer TUs) ----
    void write_escaped(std::pmr::string &sink, std::string_view s, bool ascii = false);
    void write_file(std::string_view path, std::string_view data);
}

#endif // INCLUDE_PJH_JSON_WRITER_HPP
