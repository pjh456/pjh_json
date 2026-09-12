#ifndef INCLUDE_PJH_JSON_PATCH_HPP
#define INCLUDE_PJH_JSON_PATCH_HPP

#include <cstddef>
#include <memory_resource>
#include <string>
#include <string_view>
#include <vector>

#include <pjh_result/result.hpp>

#include "config.hpp"
#include "error.hpp"
#include "json.hpp"

namespace pjh::json
{
    /**
     * @brief Decoded RFC 6901 JSON Pointer: '/'-separated reference tokens
     *        with ~1 -> '/' and ~0 -> '~' already resolved (owned strings).
     *        Empty pointer = the document root.
     * @note Owned tokens (not borrowed views): unescaping changes the byte
     *       length, so the decoded form cannot alias the source string.
     */
    using JsonPointer = std::vector<std::string>;

    /**
     * @brief Parse an RFC 6901 JSON Pointer into decoded reference tokens
     * @param pointer e.g. "/a/b/0"; empty = root
     * @return Decoded tokens; empty for the root pointer
     * @throws std::invalid_argument on grammar errors: a non-empty pointer
     *         not starting with '/', or a '~' not followed by '0' or '1'
     * @note Grammar differs from parse_path (path.hpp): '/' separator,
     *       ~0/~1 escaping, strict array indices, and the '-' append token
     *       are pointer-only. Decoding order: ~1 first, then ~0 (so "~01"
     *       decodes to "~1", not "/").
     */
    [[nodiscard]] JsonPointer parse_json_pointer(std::string_view pointer);

    /**
     * @brief Apply an RFC 6902 JSON Patch (Result primitive)
     * @param target Document to patch in place (untouched on failure)
     * @param patch Patch document (an array of operation objects)
     * @param res Resource for every value/container/key the patch creates;
     *        must outlive target after the call. nullptr falls back to the
     *        global config resource.
     * @return Ok on success; Err with the structured op/path failure otherwise
     * @throws std::bad_alloc on allocation failure (never converted; target
     *         stays untouched because the patch runs on a shadow copy)
     * @note Atomic: on any failure (including bad_alloc) target is unchanged.
     *       Implemented by cloning target into res, applying to the clone,
     *       then noexcept move-assigning back.
     * @note Cost is O(size(target)) for the atomically safe shadow copy.
     * @note Pass doc.resource() when target is inside a Document so the new
     *       nodes stay in the document arena.
     */
    [[nodiscard]] pjh::result::Result<void, PatchError>
    patch_result(Json &target, const Json &patch,
                 std::pmr::memory_resource *res = Config::instance().resource());

    /**
     * @brief Apply an RFC 6902 JSON Patch (throwing compatibility shell)
     * @param target Document to patch in place (untouched on failure)
     * @param patch Patch document (an array of operation objects)
     * @param res Resource for patched values; must outlive target
     * @throws PatchError on any patch application failure
     * @throws std::bad_alloc on allocation failure
     * @note Thin shell over patch_result(); same atomicity contract.
     */
    void patch(Json &target, const Json &patch,
               std::pmr::memory_resource *res = Config::instance().resource());

    /**
     * @brief Apply an RFC 7386 JSON Merge Patch in place
     * @param target Value to merge into
     * @param patch Merge patch value
     * @param res Resource for created values/keys; must outlive target.
     *        nullptr falls back to the global config resource.
     * @throws std::bad_alloc on allocation failure (may leave a partial
     *         merge; RFC 7386 has no other failure mode)
     * @note patch non-object replaces target wholesale (cloned into res);
     *       a null member deletes the target member; objects merge
     *       recursively. Unlike Object::merge, null deletes rather than
     *       overwrites. No atomicity guarantee (no semantic failure exists).
     */
    void merge_patch(Json &target, const Json &patch, std::pmr::memory_resource *res = Config::instance().resource());
}

#endif // INCLUDE_PJH_JSON_PATCH_HPP
