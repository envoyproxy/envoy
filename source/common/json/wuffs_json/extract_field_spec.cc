#include "source/common/json/wuffs_json/extract_field_spec.h"

#include <algorithm>

#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Json {
namespace Wuffs {

absl::StatusOr<ExtractFieldSpec> parseExtractFieldSpec(absl::string_view path) {
  if (path.empty()) {
    return absl::InvalidArgumentError("extract_field_spec: path must not be empty");
  }
  if (path.front() == '.') {
    return absl::InvalidArgumentError("extract_field_spec: path must not start with '.'");
  }
  if (path.back() == '.') {
    return absl::InvalidArgumentError("extract_field_spec: path must not end with '.'");
  }

  ExtractFieldSpec out;
  out.path = std::string(path);

  std::string current_key;
  bool in_array = false;

  for (size_t i = 0; i < path.size(); ++i) {
    const char c = path[i];
    switch (c) {
    case '[':
      if (in_array) {
        return absl::InvalidArgumentError("extract_field_spec: nested '[' in path");
      }
      // '.' immediately before '[' is invalid: buildPatternPath never produces
      // this sequence — '[]' always directly follows the parent dict key.
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
      } else if (path[i - 1] == '.') {
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
  return out;
}

std::string ExtractFieldSpec::canonicalPath() const {
  std::string path;
  for (const auto& seg : segments) {
    if (seg.is_array_element) {
      path += "[]";
    } else {
      // Dict key: prepend '.' when path is non-empty, matching buildPatternPath().
      if (!path.empty()) {
        path += '.';
      }
      path += seg.key;
    }
  }
  return path;
}

int DecoderConfig::requiredMaxDepth() const {
  int max = 0;
  for (const auto& spec : extract_fields) {
    max = std::max(max, spec.depth());
  }
  return max;
}

} // namespace Wuffs
} // namespace Json
} // namespace Envoy
