#ifndef INCLUDE_PJH_JSON_WRITER_HPP
#define INCLUDE_PJH_JSON_WRITER_HPP

#include <cstdint>
#include <string_view>
#include <memory_resource>
#include <iosfwd>

#include "document.hpp"
#include <pjh_result/result.hpp>

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
     * @note dump never emits a BOM: the output starts with the first
     *       value byte. A U+FEFF code point stored in a string is data,
     *       not a prefix — it round-trips as raw UTF-8 (normal mode) or
     *       \uFEFF (ascii mode).
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
     * @brief Serialize Json value to string (result form)
     * @param value Json tree to serialize
     * @param opts  Formatting options
     * @param res   Memory resource for output string (default: global config)
     * @return Result holding the serialized string on success, or the
     *         JsonError (copied by value) on failure
     * @throws Only non-JsonError exceptions (e.g. std::bad_alloc) escape;
     *         all writer failures are JsonError by construction
     * @note Same contract as dump; thin catch-and-wrap shell.
     */
    [[nodiscard]] pjh::result::Result<std::pmr::string, JsonError> dump_result(
        const Json &value,
        const DumpOptions &opts = {},
        std::pmr::memory_resource *res = Config::instance().resource());

    /**
     * @brief Serialize Document root to string (result form)
     * @param doc  Document whose root is serialized
     * @param opts Formatting options
     * @param res  Memory resource for output string (default: global config)
     * @return Result holding the serialized string on success, or the
     *         JsonError (copied by value) on failure
     * @throws Only non-JsonError exceptions (e.g. std::bad_alloc) escape;
     *         all writer failures are JsonError by construction
     * @note Same contract as dump(Document); thin catch-and-wrap shell.
     */
    [[nodiscard]] pjh::result::Result<std::pmr::string, JsonError> dump_result(
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
      * @throws JsonError if value contains non-finite double (NaN/Inf), or the
      *                   stream write fails
      */
     void dump_to(std::ostream &os, const Json &value, const DumpOptions &opts = {});

    /**
     * @brief Serialize and write to file
     * @param path  File path
     * @param value Json tree to serialize
     * @param opts  Formatting options
      * @throws JsonError if file cannot be opened, written, or closed, or value
      *                   contains non-finite double
     */
    void dump_file(std::string_view path, const Json &value, const DumpOptions &opts = {});

    /**
     * @brief Serialize and write to file (result form)
     * @param path  File path
     * @param value Json tree to serialize
     * @param opts  Formatting options
     * @return Result holding the serialized content written to the file on
     *         success, or the JsonError (copied by value) on failure
     * @throws Only non-JsonError exceptions (e.g. std::bad_alloc) escape;
     *         all writer failures are JsonError by construction
     * @note Same contract as dump_file; thin catch-and-wrap shell. On success
     *       the payload IS the content written to the file.
     */
    [[nodiscard]] pjh::result::Result<std::pmr::string, JsonError> dump_file_result(
        std::string_view path, const Json &value, const DumpOptions &opts = {});
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
     * @brief Serialize array as JSONL (result form)
     * @param arr Array of values (each becomes one line)
     * @param res Memory resource for output string (default: global config)
     * @return Result holding the JSONL string on success, or the JsonError
     *         (copied by value) on failure
     * @throws Only non-JsonError exceptions (e.g. std::bad_alloc) escape;
     *         all writer failures are JsonError by construction
     * @note Same contract as dump_jsonl; thin catch-and-wrap shell.
     */
    [[nodiscard]] pjh::result::Result<std::pmr::string, JsonError> dump_jsonl_result(
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
      * @throws JsonError if file cannot be opened, written, or closed
      */
    void dump_jsonl_file(std::string_view path, const Array &arr);

    /**
     * @brief Write JSONL to file (result form)
     * @param path File path
     * @param arr  Array of values
     * @return Result holding the serialized content written to the file on
     *         success, or the JsonError (copied by value) on failure
     * @throws Only non-JsonError exceptions (e.g. std::bad_alloc) escape;
     *         all writer failures are JsonError by construction
     * @note Same contract as dump_jsonl_file; thin catch-and-wrap shell. On
     *       success the payload IS the content written to the file.
     */
    [[nodiscard]] pjh::result::Result<std::pmr::string, JsonError> dump_jsonl_file_result(
        std::string_view path, const Array &arr);
    /**@}*/

    /** @name Prettify (parse + dump with pretty print) */
    /**@{*/
    /**
     * @brief Parse JSON string and re-serialize with given options
     * @param json Raw JSON text
     * @param opts Formatting options (pretty=true, indent=2 by default)
     * @param res  Memory resource (default: global config)
     * @return Serialized JSON string with specified formatting
     * @throws ParseError if input is invalid JSON
     * @throws JsonError if value contains non-finite double
     */
    [[nodiscard]] std::pmr::string prettify(
        std::string_view json,
        const DumpOptions &opts = {.pretty = true, .indent = 2},
        std::pmr::memory_resource *res = Config::instance().resource());
    /**@}*/

    /** @name Internal helpers (shared by writer TUs) */
    /**@{*/
    /**
     * @brief Write JSON-escaped string (with surrounding quotes) to sink
     * @param sink  Output string (appended to)
     * @param s     Raw string to escape and emit
     * @param ascii If true, escape non-ASCII as \\uXXXX
      * @note Uses SIMD for fast scanning of characters needing escape.
      * @throws JsonError if s contains invalid UTF-8 (only when ascii=true)
      * @note The strict_utf8 Config knob does NOT apply here: non-ascii
      *       mode is a byte mirror (ill-formed UTF-8 round-trips
      *       untouched), ascii mode validates unconditionally via the
      *       pre-existing six JsonError sites (DumpOptions.ascii).
      */
    void write_escaped(std::pmr::string &sink, std::string_view s, bool ascii = false);
    /**
     * @brief Write raw data to file
     * @param path File path
      * @param data Bytes to write
      * @throws JsonError if file cannot be opened, written, or closed
      */
    void write_file(std::string_view path, std::string_view data);
    /**@}*/
}

#endif // INCLUDE_PJH_JSON_WRITER_HPP
