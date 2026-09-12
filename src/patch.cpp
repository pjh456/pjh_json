#include "pjh_json/patch.hpp"

#include <cstdint>
#include <memory_resource>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include "pjh_json/config.hpp"
#include "pjh_json/json.hpp"

namespace pjh::json
{
    // --- parse_json_pointer ---

    namespace
    {
        /*
         * Decode one RFC 6901 reference token
         *
         * Single pass over the raw token: '~1' -> '/', '~0' -> '~'. A '~'
         * not followed by '0' or '1' (including a trailing '~') is a grammar
         * error. The single pass reproduces the required decode order: "~01"
         * reads '~0' first and then a literal '1' -> "~1" (not "/").
         */
        void unescape_token(std::string_view raw, std::string &out)
        {
            out.clear();
            out.reserve(raw.size());
            for (size_t i = 0; i < raw.size(); ++i)
            {
                const char c = raw[i];
                if (c != '~')
                {
                    out.push_back(c);
                    continue;
                }
                if (i + 1 >= raw.size())
                    throw std::invalid_argument("json pointer: trailing '~'");
                const char e = raw[++i];
                if (e == '0')
                    out.push_back('~');
                else if (e == '1')
                    out.push_back('/');
                else
                    throw std::invalid_argument("json pointer: bad escape");
            }
        }
    }

    JsonPointer parse_json_pointer(std::string_view pointer)
    {
        if (pointer.empty())
            return JsonPointer{};
        if (pointer.front() != '/')
            throw std::invalid_argument("json pointer: must start with '/'");

        JsonPointer tokens;
        size_t start = 1; // past the leading '/'
        for (;;)
        {
            const size_t slash = pointer.find('/', start);
            const std::string_view raw =
                slash == std::string_view::npos
                    ? pointer.substr(start)
                    : pointer.substr(start, slash - start);
            std::string token;
            unescape_token(raw, token);
            tokens.push_back(std::move(token));
            if (slash == std::string_view::npos)
                break;
            start = slash + 1;
        }
        return tokens;
    }

    // --- patch engine (TU-local) ---

    namespace
    {
        using R = pjh::result::Result<void, PatchError>;
        using NodeResult = pjh::result::Result<Json *, PatchError>;

        [[nodiscard]] PatchError make_error(PatchErrorKind kind, size_t op_index,
                                            std::string_view op,
                                            std::string_view pointer,
                                            std::string detail = {})
        {
            return PatchError(kind, op_index, std::string(op),
                              std::string(pointer), std::move(detail));
        }

        /*
         * Strict RFC 6901 array index: 0 | [1-9][0-9]*
         *
         * Leading zeros, non-digits and size_t overflow are all rejected.
         */
        [[nodiscard]] bool strict_index(std::string_view token, size_t &out) noexcept
        {
            if (token.empty())
                return false;
            if (token.size() > 1 && token.front() == '0')
                return false;
            size_t value = 0;
            for (const char c : token)
            {
                if (c < '0' || c > '9')
                    return false;
                if (value > (SIZE_MAX - 9) / 10)
                    return false;
                value = value * 10 + static_cast<size_t>(c - '0');
            }
            out = value;
            return true;
        }

        [[nodiscard]] bool is_known_op(std::string_view op) noexcept
        {
            return op == "add" || op == "remove" || op == "replace" ||
                   op == "move" || op == "copy" || op == "test";
        }

        [[nodiscard]] bool is_proper_prefix(const JsonPointer &from,
                                            const JsonPointer &path)
        {
            if (from.size() >= path.size())
                return false;
            for (size_t i = 0; i < from.size(); ++i)
                if (from[i] != path[i])
                    return false;
            return true;
        }

        /*
         * Walk to the node a pointer denotes (root for the empty pointer)
         *
         * The parent node's runtime type decides each hop: an object parent
         * looks the token up by content, an array parent requires a strict
         * index < size, anything else is a TypeMismatch. Used for read-only
         * consumers (test source, copy/move source).
         */
        [[nodiscard]] NodeResult resolve_node(Json &root, const JsonPointer &ptr,
                                              size_t op_index, std::string_view op,
                                              std::string_view pointer)
        {
            if (ptr.empty())
                return NodeResult::Ok(&root);
            Json *cur = &root;
            for (const std::string &token : ptr)
            {
                if (cur->is_object())
                {
                    Object &obj = *cur->try_as_object();
                    if (!obj.contains(token))
                        return NodeResult::Err(make_error(
                            PatchErrorKind::PathNotFound, op_index, op, pointer));
                    cur = &obj.at(token);
                }
                else if (cur->is_array())
                {
                    size_t index = 0;
                    if (!strict_index(token, index))
                        return NodeResult::Err(make_error(
                            PatchErrorKind::InvalidArrayIndex, op_index, op,
                            pointer));
                    Array &arr = *cur->try_as_array();
                    if (index >= arr.size())
                        return NodeResult::Err(make_error(
                            PatchErrorKind::IndexOutOfRange, op_index, op,
                            pointer));
                    cur = &arr[index];
                }
                else
                    return NodeResult::Err(make_error(
                        PatchErrorKind::TypeMismatch, op_index, op, pointer));
            }
            return NodeResult::Ok(cur);
        }

        /*
         * Where the last token of a pointer lands, relative to its parent
         */
        struct Slot
        {
            Json *parent = nullptr;
            std::string token; // final reference token
            bool object = false; // parent is an Object
            size_t index = 0;    // array position (valid when !object)
        };

        enum class SlotMode
        {
            Existing, // the slot must already exist (remove/replace/test/source)
            Insert    // the slot may be created ('-' append; index == size)
        };

        /*
         * Resolve the parent of the final token
         *
         * Intermediate hops always require an existing node. The final token
         * is interpreted by the parent kind: an object key (existence checked
         * in Existing mode), or an array position ('-' append only in Insert
         * mode; strict index with <= size for Insert, < size for Existing).
         */
        [[nodiscard]] pjh::result::Result<Slot, PatchError>
        resolve_slot(Json &root, const JsonPointer &ptr, SlotMode mode,
                     size_t op_index, std::string_view op,
                     std::string_view pointer)
        {
            Json *cur = &root;
            for (size_t i = 0; i + 1 < ptr.size(); ++i)
            {
                const std::string &token = ptr[i];
                if (cur->is_object())
                {
                    Object &obj = *cur->try_as_object();
                    if (!obj.contains(token))
                        return pjh::result::Result<Slot, PatchError>::Err(
                            make_error(PatchErrorKind::PathNotFound, op_index, op,
                                       pointer));
                    cur = &obj.at(token);
                }
                else if (cur->is_array())
                {
                    size_t index = 0;
                    if (!strict_index(token, index))
                        return pjh::result::Result<Slot, PatchError>::Err(
                            make_error(PatchErrorKind::InvalidArrayIndex, op_index,
                                       op, pointer));
                    Array &arr = *cur->try_as_array();
                    if (index >= arr.size())
                        return pjh::result::Result<Slot, PatchError>::Err(
                            make_error(PatchErrorKind::IndexOutOfRange, op_index,
                                       op, pointer));
                    cur = &arr[index];
                }
                else
                    return pjh::result::Result<Slot, PatchError>::Err(make_error(
                        PatchErrorKind::TypeMismatch, op_index, op, pointer));
            }

            Slot slot;
            slot.parent = cur;
            slot.token = ptr.back();
            if (cur->is_object())
            {
                slot.object = true;
                if (mode == SlotMode::Existing &&
                    !cur->try_as_object()->contains(slot.token))
                    return pjh::result::Result<Slot, PatchError>::Err(make_error(
                        PatchErrorKind::PathNotFound, op_index, op, pointer));
                return pjh::result::Result<Slot, PatchError>::Ok(std::move(slot));
            }
            if (cur->is_array())
            {
                slot.object = false;
                Array &arr = *cur->try_as_array();
                if (slot.token == "-")
                {
                    if (mode != SlotMode::Insert)
                        return pjh::result::Result<Slot, PatchError>::Err(
                            make_error(PatchErrorKind::InvalidArrayIndex, op_index,
                                       op, pointer));
                    slot.index = arr.size();
                    return pjh::result::Result<Slot, PatchError>::Ok(std::move(slot));
                }
                size_t index = 0;
                if (!strict_index(slot.token, index))
                    return pjh::result::Result<Slot, PatchError>::Err(make_error(
                        PatchErrorKind::InvalidArrayIndex, op_index, op, pointer));
                const bool too_far =
                    mode == SlotMode::Insert ? index > arr.size()
                                             : (arr.empty() || index >= arr.size());
                if (too_far)
                    return pjh::result::Result<Slot, PatchError>::Err(make_error(
                        PatchErrorKind::IndexOutOfRange, op_index, op, pointer));
                slot.index = index;
                return pjh::result::Result<Slot, PatchError>::Ok(std::move(slot));
            }
            return pjh::result::Result<Slot, PatchError>::Err(make_error(
                PatchErrorKind::TypeMismatch, op_index, op, pointer));
        }

        /*
         * Insert an already-owned value at a pointer (add/move/copy tail)
         *
         * The empty pointer replaces the whole document. Array insert uses
         * the public data() escape (Array has no insert member); the index
         * was validated by resolve_slot.
         */
        [[nodiscard]] R insert_at(Json &shadow, const JsonPointer &ptr,
                                  std::string_view pointer, Json value,
                                  size_t op_index, std::string_view op,
                                  std::pmr::memory_resource *res)
        {
            if (ptr.empty())
            {
                shadow = std::move(value);
                return R::Ok();
            }
            auto resolved = resolve_slot(shadow, ptr, SlotMode::Insert, op_index,
                                         op, pointer);
            if (resolved.is_err())
                return R::Err(std::move(resolved).unwrap_err());
            Slot slot = std::move(resolved).unwrap();
            if (slot.object)
                slot.parent->try_as_object()->insert(slot.token, std::move(value),
                                                     res);
            else
            {
                Array &arr = *slot.parent->try_as_array();
                arr.data().insert(
                    arr.data().begin() +
                        static_cast<std::pmr::vector<Json>::difference_type>(
                            slot.index),
                    std::move(value));
            }
            return R::Ok();
        }

        /*
         * Apply one RFC 6902 operation object to the shadow document
         *
         * Pre-conditions from validate_patch_shape() hold (op is an object and
         * its "op"/"path" members are present strings); the checks are
         * repeated defensively so apply_one stays safe on its own.
         */
        [[nodiscard]] R apply_one(Json &shadow, const Json &op, size_t op_index,
                                  std::pmr::memory_resource *res)
        {
            const Object *obj = op.try_as_object();
            if (obj == nullptr)
                return R::Err(make_error(PatchErrorKind::InvalidPatchDocument,
                                         op_index, "", "",
                                         "operation must be an object"));
            const auto name_opt =
                obj->contains("op") ? obj->at("op").try_as_string()
                                    : std::nullopt;
            const auto path_opt =
                obj->contains("path") ? obj->at("path").try_as_string()
                                      : std::nullopt;
            if (!name_opt || !path_opt)
                return R::Err(make_error(PatchErrorKind::InvalidPatchDocument,
                                         op_index, "", "",
                                         "operation missing string \"op\"/\"path\""));
            const std::string_view op_name = *name_opt;
            const std::string_view path_text = *path_opt;
            if (!is_known_op(op_name))
                return R::Err(make_error(PatchErrorKind::UnknownOp, op_index,
                                         op_name, path_text));

            const JsonPointer path = parse_json_pointer(path_text);

            if (op_name == "add")
            {
                if (!obj->contains("value"))
                    return R::Err(make_error(PatchErrorKind::InvalidPatchDocument,
                                             op_index, op_name, path_text,
                                             "missing \"value\""));
                return insert_at(shadow, path, path_text,
                                 obj->at("value").clone(res), op_index, op_name,
                                 res);
            }
            if (op_name == "remove")
            {
                if (path.empty())
                    return R::Err(make_error(PatchErrorKind::RootOperation,
                                             op_index, op_name, path_text));
                auto resolved = resolve_slot(shadow, path, SlotMode::Existing,
                                             op_index, op_name, path_text);
                if (resolved.is_err())
                    return R::Err(std::move(resolved).unwrap_err());
                Slot slot = std::move(resolved).unwrap();
                if (slot.object)
                    slot.parent->try_as_object()->remove(slot.token);
                else
                    slot.parent->try_as_array()->erase(slot.index);
                return R::Ok();
            }
            if (op_name == "replace")
            {
                if (!obj->contains("value"))
                    return R::Err(make_error(PatchErrorKind::InvalidPatchDocument,
                                             op_index, op_name, path_text,
                                             "missing \"value\""));
                Json value = obj->at("value").clone(res);
                if (path.empty())
                {
                    shadow = std::move(value);
                    return R::Ok();
                }
                auto resolved = resolve_slot(shadow, path, SlotMode::Existing,
                                             op_index, op_name, path_text);
                if (resolved.is_err())
                    return R::Err(std::move(resolved).unwrap_err());
                Slot slot = std::move(resolved).unwrap();
                if (slot.object)
                    slot.parent->try_as_object()->insert(slot.token,
                                                         std::move(value), res);
                else
                    (*slot.parent->try_as_array())[slot.index] = std::move(value);
                return R::Ok();
            }
            if (op_name == "test")
            {
                if (!obj->contains("value"))
                    return R::Err(make_error(PatchErrorKind::InvalidPatchDocument,
                                             op_index, op_name, path_text,
                                             "missing \"value\""));
                auto node = resolve_node(shadow, path, op_index, op_name, path_text);
                if (node.is_err())
                    return R::Err(std::move(node).unwrap_err());
                if (!(*node.unwrap() == obj->at("value")))
                    return R::Err(make_error(PatchErrorKind::TestFailed,
                                             op_index, op_name, path_text));
                return R::Ok();
            }

            if (!obj->contains("from"))
                return R::Err(make_error(PatchErrorKind::InvalidPatchDocument,
                                         op_index, op_name, path_text,
                                         "missing string \"from\""));
            const auto from_opt = obj->at("from").try_as_string();
            if (!from_opt)
                return R::Err(make_error(PatchErrorKind::InvalidPatchDocument,
                                         op_index, op_name, path_text,
                                         "missing string \"from\""));
            const std::string_view from_text = *from_opt;
            const JsonPointer from = parse_json_pointer(from_text);

            if (op_name == "copy")
            {
                auto src = resolve_node(shadow, from, op_index, op_name, from_text);
                if (src.is_err())
                    return R::Err(std::move(src).unwrap_err());
                return insert_at(shadow, path, path_text,
                                 src.unwrap()->clone(res), op_index, op_name,
                                 res);
            }

            // move: RFC 6902 4.4 remove-then-add, path re-resolved after removal
            if (from.empty())
                return R::Err(make_error(PatchErrorKind::RootOperation, op_index,
                                         op_name, from_text));
            if (from == path)
                return R::Ok(); // same location: defined no-op
            if (is_proper_prefix(from, path))
                return R::Err(make_error(PatchErrorKind::MoveIntoDescendant,
                                         op_index, op_name, path_text));
            auto resolved = resolve_slot(shadow, from, SlotMode::Existing,
                                         op_index, op_name, from_text);
            if (resolved.is_err())
                return R::Err(std::move(resolved).unwrap_err());
            Slot slot = std::move(resolved).unwrap();
            Json moved;
            if (slot.object)
            {
                Object &src_obj = *slot.parent->try_as_object();
                moved = std::move(src_obj.at(slot.token));
                src_obj.remove(slot.token);
            }
            else
            {
                Array &src_arr = *slot.parent->try_as_array();
                moved = std::move(src_arr[slot.index]);
                src_arr.erase(slot.index);
            }
            return insert_at(shadow, path, path_text, std::move(moved), op_index,
                             op_name, res);
        }

        /*
         * Structural pre-scan: reject a malformed patch document before the
         * O(|target|) clone (top level, op shape, required members, pointer
         * syntax). Semantic failures stay shadow-guaranteed.
         */
        [[nodiscard]] std::optional<PatchError>
        validate_patch_shape(const Array &ops)
        {
            for (size_t i = 0; i < ops.size(); ++i)
            {
                const Json &op = ops[i];
                const Object *obj = op.try_as_object();
                if (obj == nullptr)
                    return make_error(PatchErrorKind::InvalidPatchDocument, i,
                                      "", "", "operation must be an object");
                const auto name_opt =
                    obj->contains("op") ? obj->at("op").try_as_string()
                                        : std::nullopt;
                if (!name_opt)
                    return make_error(PatchErrorKind::InvalidPatchDocument, i,
                                      "", "", "operation missing string \"op\"");
                const std::string_view op_name = *name_opt;
                const auto path_opt =
                    obj->contains("path") ? obj->at("path").try_as_string()
                                          : std::nullopt;
                const std::string_view path_text =
                    path_opt ? *path_opt : std::string_view{};
                if (!is_known_op(op_name))
                    return make_error(PatchErrorKind::UnknownOp, i, op_name,
                                      path_text);
                if (!path_opt)
                    return make_error(PatchErrorKind::InvalidPatchDocument, i,
                                      op_name, "",
                                      "operation missing string \"path\"");
                try
                {
                    (void)parse_json_pointer(path_text);
                }
                catch (const std::invalid_argument &)
                {
                    return make_error(PatchErrorKind::InvalidPointer, i, op_name,
                                      path_text, "bad \"path\" pointer");
                }
                if (op_name == "add" || op_name == "replace" ||
                    op_name == "test")
                {
                    if (!obj->contains("value"))
                        return make_error(PatchErrorKind::InvalidPatchDocument, i,
                                          op_name, path_text,
                                          "operation missing \"value\"");
                    continue;
                }
                if (op_name == "remove")
                    continue;
                const auto from_opt =
                    obj->contains("from") ? obj->at("from").try_as_string()
                                          : std::nullopt;
                if (!from_opt)
                    return make_error(PatchErrorKind::InvalidPatchDocument, i,
                                      op_name, path_text,
                                      "operation missing string \"from\"");
                const std::string_view from_text = *from_opt;
                try
                {
                    (void)parse_json_pointer(from_text);
                }
                catch (const std::invalid_argument &)
                {
                    return make_error(PatchErrorKind::InvalidPointer, i, op_name,
                                      from_text, "bad \"from\" pointer");
                }
            }
            return std::nullopt;
        }
    }

    // --- patch_result / patch ---

    pjh::result::Result<void, PatchError>
    patch_result(Json &target, const Json &patch, std::pmr::memory_resource *res)
    {
        if (!res)
            res = Config::instance().resource();
        if (!patch.is_array())
            return R::Err(make_error(PatchErrorKind::InvalidPatchDocument, 0, "",
                                     "", "top-level patch must be an array"));
        const Array &ops = *patch.try_as_array();
        if (ops.empty())
            return R::Ok();

        if (auto error = validate_patch_shape(ops))
            return R::Err(std::move(*error));

        Json shadow = target.clone(res);
        for (size_t i = 0; i < ops.size(); ++i)
        {
            auto applied = apply_one(shadow, ops[i], i, res);
            if (applied.is_err())
                return R::Err(std::move(applied).unwrap_err());
        }
        target = std::move(shadow);
        return R::Ok();
    }

    void patch(Json &target, const Json &patch, std::pmr::memory_resource *res)
    {
        auto result = patch_result(target, patch, res);
        if (result.is_err())
            throw std::move(result).unwrap_err();
    }
}
