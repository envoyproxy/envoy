#include "source/common/json/wuffs_json/parser_config.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Json {
namespace Wuffs {
namespace {

using Segment = std::optional<std::string>;

// Rejects structural problems detectable before character-by-character parsing.
absl::Status validatePathSyntax(absl::string_view path) {
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
  return absl::OkStatus();
}

absl::Status appendSegment(std::vector<Segment>& segments, Segment segment, int max_depth) {
  if (max_depth > 0 && segments.size() >= static_cast<size_t>(max_depth)) {
    return absl::InvalidArgumentError(absl::StrCat("extract_field_spec: path depth ",
                                                   segments.size() + 1,
                                                   " exceeds maximum supported depth ", max_depth));
  }
  segments.push_back(std::move(segment));
  return absl::OkStatus();
}

class ExtractFieldSpecParser {
public:
  ExtractFieldSpecParser(absl::string_view path, int max_depth)
      : path_(path), max_depth_(max_depth) {}

  absl::StatusOr<std::vector<Segment>> parse() {
    std::vector<Segment> segments;
    for (size_t i = 0; i < path_.size(); ++i) {
      if (auto status = consume(i, segments); !status.ok()) {
        return status;
      }
    }
    if (in_array_) {
      return absl::InvalidArgumentError("extract_field_spec: unterminated '[' in path");
    }
    if (auto status = flushKey(path_.size(), segments); !status.ok()) {
      return status;
    }
    if (segments.empty()) {
      return absl::InvalidArgumentError("extract_field_spec: path contains no segments");
    }
    return segments;
  }

private:
  absl::Status consume(size_t i, std::vector<Segment>& segments) {
    switch (path_[i]) {
    case '[':
      return openArray(i, segments);
    case ']':
      return closeArray(segments);
    case '.':
      return separator(i, segments);
    default:
      return keyCharacter(i);
    }
  }

  absl::Status openArray(size_t i, std::vector<Segment>& segments) {
    if (in_array_) {
      return absl::InvalidArgumentError("extract_field_spec: nested '[' in path");
    }
    // '.' immediately before '[' is invalid: the canonical pattern-path
    // syntax never contains '.[]' — '[]' always directly follows the parent dict key.
    if (i > 0 && path_[i - 1] == '.') {
      return absl::InvalidArgumentError(
          "extract_field_spec: '.' before '[' is not valid; use 'key[]' not 'key.[]'");
    }
    if (auto status = flushKey(i, segments); !status.ok()) {
      return status;
    }
    in_array_ = true;
    return absl::OkStatus();
  }

  absl::Status closeArray(std::vector<Segment>& segments) {
    if (!in_array_) {
      return absl::InvalidArgumentError("extract_field_spec: unexpected ']' in path");
    }
    if (auto status = appendSegment(segments, std::nullopt, max_depth_); !status.ok()) {
      return status;
    }
    in_array_ = false;
    return absl::OkStatus();
  }

  absl::Status separator(size_t i, std::vector<Segment>& segments) {
    if (in_array_) {
      return absl::InvalidArgumentError("extract_field_spec: '.' inside '[]' in path");
    }
    if (!has_key_ && i > 0 && path_[i - 1] == '.') {
      return absl::InvalidArgumentError(
          absl::StrCat("extract_field_spec: consecutive '.' at position ", i, " in path"));
    }
    return flushKey(i, segments);
  }

  absl::Status keyCharacter(size_t i) {
    if (in_array_) {
      // Index-based addressing (e.g. "[42]") is intentionally unsupported.
      // Path specs are structural patterns: '[]' means "every element" because
      // LLM/MCP arrays (messages[], choices[], result.content[]) are homogeneous
      // lists where every element is processed. 
      // We can implement index-based addressing if needed in the future.
      return absl::InvalidArgumentError(
          "extract_field_spec: only '[]' wildcard is supported, not '[...]'");
    }
    // A key may not start directly after ']': the canonical pattern-path
    // syntax always has a '.' separator between an array wildcard and a
    // following dict key ("a[].b", never "a[]b").
    if (i > 0 && path_[i - 1] == ']') {
      return absl::InvalidArgumentError("extract_field_spec: missing '.' between ']' and key");
    }
    if (!has_key_) {
      segment_start_ = i;
      has_key_ = true;
    }
    return absl::OkStatus();
  }

  absl::Status flushKey(size_t end, std::vector<Segment>& segments) {
    if (!has_key_) {
      return absl::OkStatus();
    }
    has_key_ = false;
    return appendSegment(segments, std::string(path_.substr(segment_start_, end - segment_start_)),
                         max_depth_);
  }

  absl::string_view path_;
  const int max_depth_;
  bool in_array_{false};
  bool has_key_{false};
  size_t segment_start_{0};
};

} // namespace

absl::StatusOr<ExtractFieldSpec> parseExtractFieldSpec(absl::string_view path, int max_depth) {
  if (auto status = validatePathSyntax(path); !status.ok()) {
    return status;
  }
  auto segments = ExtractFieldSpecParser(path, max_depth).parse();
  if (!segments.ok()) {
    return segments.status();
  }
  ExtractFieldSpec out;
  out.segments = std::move(*segments);
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
