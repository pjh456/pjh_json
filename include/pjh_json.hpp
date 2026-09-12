#ifndef INCLUDE_PJH_JSON_HPP
#define INCLUDE_PJH_JSON_HPP

/// @file pjh_json.hpp
/// @brief Umbrella header exposing the complete runtime pjh_json API.
/// @details Includes the `Json` value type, `Document`, `Parser`, `Writer`,
///          `Access`, `Path`, and `Config` modules. The compile-time API
///          (`ConstJson`) is opt-in: include `pjh_json/json_constexpr.hpp`
///          explicitly when you need compile-time construction/validation.

#include "pjh_json/access.hpp"
#include "pjh_json/array.hpp"
#include "pjh_json/config.hpp"
#include "pjh_json/document.hpp"
#include "pjh_json/error.hpp"
#include "pjh_json/json.hpp"
#include "pjh_json/json_fwd.hpp"
#include "pjh_json/object.hpp"
#include "pjh_json/parser.hpp"
#include "pjh_json/path.hpp"
#include "pjh_json/std_interop.hpp"
#include "pjh_json/stream.hpp"
#include "pjh_json/string.hpp"
#include "pjh_json/version.hpp"
#include "pjh_json/writer.hpp"

#endif // INCLUDE_PJH_JSON_HPP
