#include "source/common/json/wuffs_json/parser_config.h"

#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Json {
namespace Wuffs {

absl::StatusOr<ExtractFieldSpec> parseExtractFieldSpec(absl::string_view path, int max_depth) {
  if (path.empty()) {
    return absl::InvalidArgumentError("extract_field_spec: path must not be empty");
  }

  if (path.front() == '.') {
    return absl::InvalidArgumentError("extract_field_spec: path must not start with '.'");
  }
  if (path.back() == '.') {
    return absl::InvalidArgumentError("extract_field_spec: path must not end with '.'");
  }
  // '\' is reserved for a future escape syntax for document keys containing
  // '.', '[', ']' Rejecting it now keeps that extension non-breaking.
  if (path.find('\\') != absl::string_view::npos) {
    return absl::InvalidArgumentError(
        "extract_field_spec: '\\' is reserved for future escape syntax");
  }

  ExtractFieldSpec out;
  std::string current_key;
  bool in_array = false;

  for (size_t i = 0; i < path.size(); ++i) {
    const char c = path[i];
    switch (c) {
    case '[':
      if (in_array) {
        return absl::InvalidArgumentError("extract_field_spec: nested '[' in path");
      }
      // '.' immediately before '[' is invalid: the canonical pattern-path
      // syntax never contains '.[]' — '[]' always directly follows the
      // parent dict key.
      if (i > 0 && path[i - 1] == '.') {
        return absl::InvalidArgumentError(
            "extract_field_spec: '.' before '[' is not valid; use 'key[]' not 'key.[]'");
      }
      if (!current_key.empty()) {
        out.segments.push_back({std::move(current_key), false});
        current_key.clear();
      }
      in_array = true;
      break;
    case ']':
      if (!in_array) {
        return absl::InvalidArgumentError("extract_field_spec: unexpected ']' in path");
      }
      out.segments.push_back({"", true});
      in_array = false;
      break;
    case '.':
      if (in_array) {
        return absl::InvalidArgumentError("extract_field_spec: '.' inside '[]' in path");
      }
      if (!current_key.empty()) {
        out.segments.push_back({std::move(current_key), false});
        current_key.clear();
      } else if (i > 0 && path[i - 1] == '.') {
        return absl::InvalidArgumentError(
            absl::StrCat("extract_field_spec: consecutive '.' at position ", i, " in path"));
      }
      // Separator after ']' or between dict keys — fall through.
      break;
    default:
      if (in_array) {
        return absl::InvalidArgumentError(
            "extract_field_spec: only '[]' wildcard is supported, not '[...]'");
      }
      // A key may not start directly after ']': the canonical pattern-path
      // syntax always has a '.' separator between an array wildcard and a
      // following dict key ("a[].b", never "a[]b").
      if (i > 0 && path[i - 1] == ']') {
        return absl::InvalidArgumentError("extract_field_spec: missing '.' between ']' and key");
      }
      current_key += c;
      break;
    }
  }

  if (in_array) {
    return absl::InvalidArgumentError("extract_field_spec: unterminated '[' in path");
  }
  if (!current_key.empty()) {
    out.segments.push_back({std::move(current_key), false});
  }
  if (out.segments.empty()) {
    return absl::InvalidArgumentError("extract_field_spec: path contains no segments");
  }
  if (max_depth > 0 && static_cast<int>(out.segments.size()) > max_depth) {
    return absl::InvalidArgumentError(absl::StrCat("extract_field_spec: path depth ",
                                                   out.segments.size(),
                                                   " exceeds maximum supported depth ", max_depth));
  }
  return out;
}

absl::Status ParserConfig::validate() const {
  if (capture_all_scalars && !extract_fields.empty()) {
    return absl::InvalidArgumentError("parser_config: capture_all_scalars and extract_fields are "
                                      "mutually exclusive; set at most one extraction mode");
  }
  return absl::OkStatus();
}

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
