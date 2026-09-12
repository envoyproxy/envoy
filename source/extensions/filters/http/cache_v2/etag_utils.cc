#include "source/extensions/filters/http/cache_v2/etag_utils.h"

#include <cstdint>
#include <optional>

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CacheV2 {
namespace EtagUtils {
namespace {

struct EntityTag {
  bool weak_;
  absl::string_view opaque_tag_;
};

struct ParsedEntityTag {
  EntityTag tag_;
  absl::string_view remaining_;
};

absl::string_view trimLeadingWhitespace(absl::string_view value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
    value.remove_prefix(1);
  }
  return value;
}

// Parses the complete entity-tag grammar from RFC 9110 section 8.8.3:
// entity-tag = [ weak ] opaque-tag
// weak       = %s"W/"
// opaque-tag = DQUOTE *etagc DQUOTE
// https://www.rfc-editor.org/rfc/rfc9110.html#section-8.8.3
std::optional<ParsedEntityTag> parsePrefix(absl::string_view value) {
  value = trimLeadingWhitespace(value);
  bool weak = false;
  // The %s prefix in the ABNF makes the weakness indicator case-sensitive, so "w/" is invalid.
  if (value.starts_with("W/")) {
    weak = true;
    value.remove_prefix(2);
  }

  if (value.empty() || value.front() != '"') {
    return std::nullopt;
  }

  for (size_t i = 1; i < value.size(); ++i) {
    // Convert to an unsigned byte before checking obs-text so values from 0x80 through 0xff do not
    // become negative on platforms where char is signed.
    const uint8_t byte = static_cast<uint8_t>(value[i]);
    if (byte == '"') {
      return ParsedEntityTag{{weak, value.substr(1, i - 1)}, value.substr(i + 1)};
    }
    // RFC 9110 section 8.8.3 defines etagc as %x21 / %x23-7E / obs-text (%x80-FF):
    // https://www.rfc-editor.org/rfc/rfc9110.html#section-8.8.3
    if (byte != 0x21 && !(byte >= 0x23 && byte <= 0x7e) && byte < 0x80) {
      return std::nullopt;
    }
  }
  return std::nullopt;
}

std::optional<EntityTag> parse(absl::string_view value) {
  const std::optional<ParsedEntityTag> parsed = parsePrefix(value);
  if (!parsed.has_value() || !trimLeadingWhitespace(parsed->remaining_).empty()) {
    return std::nullopt;
  }
  return parsed->tag_;
}

} // namespace

bool strongMatch(absl::string_view lhs, absl::string_view rhs) {
  const std::optional<EntityTag> lhs_tag = parse(lhs);
  const std::optional<EntityTag> rhs_tag = parse(rhs);
  return lhs_tag.has_value() && rhs_tag.has_value() && !lhs_tag->weak_ && !rhs_tag->weak_ &&
         lhs_tag->opaque_tag_ == rhs_tag->opaque_tag_;
}

bool weakMatch(absl::string_view lhs, absl::string_view rhs) {
  const std::optional<EntityTag> lhs_tag = parse(lhs);
  const std::optional<EntityTag> rhs_tag = parse(rhs);
  return lhs_tag.has_value() && rhs_tag.has_value() && lhs_tag->opaque_tag_ == rhs_tag->opaque_tag_;
}

bool ifNoneMatch(absl::string_view field_value, absl::string_view etag) {
  field_value = trimLeadingWhitespace(field_value);
  if (!field_value.empty() && field_value.front() == '*') {
    field_value.remove_prefix(1);
    return trimLeadingWhitespace(field_value).empty();
  }

  const std::optional<EntityTag> selected_tag = parse(etag);
  if (!selected_tag.has_value()) {
    return false;
  }

  bool matched = false;
  bool parsed_any = false;
  while (true) {
    field_value = trimLeadingWhitespace(field_value);
    // RFC list syntax permits empty elements, so skip commas before parsing the next entity tag.
    while (!field_value.empty() && field_value.front() == ',') {
      field_value.remove_prefix(1);
      field_value = trimLeadingWhitespace(field_value);
    }
    if (field_value.empty()) {
      return parsed_any && matched;
    }

    const std::optional<ParsedEntityTag> parsed = parsePrefix(field_value);
    if (!parsed.has_value()) {
      return false;
    }
    parsed_any = true;
    matched |= parsed->tag_.opaque_tag_ == selected_tag->opaque_tag_;
    field_value = trimLeadingWhitespace(parsed->remaining_);
    if (!field_value.empty() && field_value.front() != ',') {
      return false;
    }
  }
}

} // namespace EtagUtils
} // namespace CacheV2
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
