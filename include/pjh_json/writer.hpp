#ifndef INCLUDE_PJH_JSON_WRITER_HPP
#define INCLUDE_PJH_JSON_WRITER_HPP

#include <cstdint>
#include <string_view>
#include <memory_resource>
#include <iosfwd>

#include "document.hpp"

namespace pjh::json
{
    /**
     * @brief Serialization options
     */
    struct DumpOptions
    {
        bool pretty = false;    // false = compact, true = indented
        uint8_t indent = 2;     // spaces per level when pretty
        char indent_char = ' '; // ' ' or '\t'
        bool ascii = false;     // true = escape non-ASCII as \uXXXX
        bool sort_keys = false; // true = emit object keys sorted
    };

    /** @name Value dump */
    /**@{*/
    /**
     * @brief Serialize Json value to string
     * @param value Json tree to serialize
     * @param opts  Formatting options
     * @param res   Memory resource for output string (default: global config)
     * @return Serialized JSON string
     * @throws JsonError if value contains non-finite double (NaN/Inf)
     */
    [[nodiscard]] std::pmr::string dump(
        const Json &value,
        const DumpOptions &opts = {},
        std::pmr::memory_resource *res = Config::instance().resource());

    /**
     * @brief Serialize Document root to string
     * @param doc  Document whose root is serialized
     * @param opts Formatting options
     * @param res  Memory resource for output string (default: global config)
     * @return Serialized JSON string
     * @throws JsonError if root contains non-finite double (NaN/Inf)
     */
    [[nodiscard]] std::pmr::string dump(
        const Document &doc,
        const DumpOptions &opts = {},
        std::pmr::memory_resource *res = Config::instance().resource());

    /**
     * @brief Append serialized value to existing string
     * @param sink Output string (appended to)
     * @param value Json tree to serialize
     * @param opts  Formatting options
     * @throws JsonError if value contains non-finite double (NaN/Inf)
     */
    void dump_to(std::pmr::string &sink, const Json &value, const DumpOptions &opts = {});

    /**
     * @brief Serialize to output stream
     * @param os    Output stream
     * @param value Json tree to serialize
     * @param opts  Formatting options
     * @throws JsonError if value contains non-finite double (NaN/Inf)
     */
    void dump_to(std::ostream &os, const Json &value, const DumpOptions &opts = {});

    /**
     * @brief Serialize and write to file
     * @param path  File path
     * @param value Json tree to serialize
     * @param opts  Formatting options
     * @throws JsonError if file cannot be opened or written, or value contains
     *                   non-finite double
     */
    void dump_file(std::string_view path, const Json &value, const DumpOptions &opts = {});
    /**@}*/

    /** @name JSONL (Array, one element per line, always compact) */
    /**@{*/
    /**
     * @brief Serialize array as JSONL (one Json per line)
     * @param arr Array of values (each becomes one line)
     * @param res Memory resource for output string (default: global config)
     * @return JSONL string (each element compact, lines separated by \\n)
     * @throws JsonError if any element contains non-finite double
     */
    [[nodiscard]] std::pmr::string dump_jsonl(
        const Array &arr,
        std::pmr::memory_resource *res = Config::instance().resource());

    /**
     * @brief Append JSONL to existing string
     * @param sink Output string (appended to)
     * @param arr  Array of values
     */
    void dump_jsonl_to(std::pmr::string &sink, const Array &arr);

    /**
     * @brief Write JSONL to file
     * @param path File path
     * @param arr  Array of values
     * @throws JsonError if file cannot be opened or written
     */
    void dump_jsonl_file(std::string_view path, const Array &arr);
    /**@}*/

    /** @name Prettify (parse + dump with pretty print) */
    /**@{*/
    /**
     * @brief Parse JSON string and re-serialize with indentation
     * @param json   Raw JSON text
     * @param indent Indentation depth (default 2)
     * @param res    Memory resource (default: global config)
     * @return Pretty-printed JSON string
     * @throws ParseError if input is invalid JSON
     * @throws JsonError if value contains non-finite double
     */
    [[nodiscard]] std::pmr::string prettify(
        std::string_view json,
        uint8_t indent = 2,
        std::pmr::memory_resource *res = Config::instance().resource());
    /**@}*/

    /** @name Internal helpers (shared by writer TUs) */
    /**@{*/
    /**
     * @brief Write JSON-escaped string (with surrounding quotes) to sink
     * @param sink  Output string (appended to)
     * @param s     Raw string to escape and emit
     * @param ascii If true, escape non-ASCII as \\uXXXX
     * @note Uses SIMD (xsimd) for fast scanning of characters needing escape.
     * @throws JsonError if s contains invalid UTF-8 (only when ascii=true)
     */
    void write_escaped(std::pmr::string &sink, std::string_view s, bool ascii = false);
    /**
     * @brief Write raw data to file
     * @param path File path
     * @param data Bytes to write
     * @throws JsonError if file cannot be opened or written
     */
    void write_file(std::string_view path, std::string_view data);
    /**@}*/
}

#endif // INCLUDE_PJH_JSON_WRITER_HPP
