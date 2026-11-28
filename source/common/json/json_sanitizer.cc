#include "source/common/json/json_sanitizer.h"

#include <cstdint>

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"
#include "third_party/utf8_range/utf8_validity.h"

namespace Envoy {
namespace Json {

namespace {

constexpr char kHexDigits[] = "0123456789abcdef";

inline void appendHexEscape(std::string& buffer, uint8_t c) {
  const char escaped[6] = {'\\', 'u', '0', '0', kHexDigits[c >> 4], kHexDigits[c & 0x0f]};
  buffer.append(escaped, sizeof(escaped));
}

// Escapes a structurally valid UTF-8 (or pure 7-bit ASCII) string per RFC 8259 without throwing
// exceptions or allocating intermediate buffers. Valid multi-byte UTF-8 sequences (>= 0x80) pass
// through unchanged.
void escapeValidUtf8(std::string& buffer, absl::string_view str) {
  buffer.clear();
  buffer.reserve(str.size() + 16);
  const char* const data = str.data();
  const size_t size = str.size();
  size_t span_start = 0;

  for (size_t i = 0; i < size; ++i) {
    const uint8_t c = static_cast<uint8_t>(data[i]);
    if (c >= 0x20 && c != '"' && c != '\\') {
      continue;
    }
    if (i > span_start) {
      buffer.append(data + span_start, i - span_start);
    }
    switch (c) {
    case '"':
      buffer.append("\\\"", 2);
      break;
    case '\\':
      buffer.append("\\\\", 2);
      break;
    case '\b':
      buffer.append("\\b", 2);
      break;
    case '\f':
      buffer.append("\\f", 2);
      break;
    case '\n':
      buffer.append("\\n", 2);
      break;
    case '\r':
      buffer.append("\\r", 2);
      break;
    case '\t':
      buffer.append("\\t", 2);
      break;
    default:
      appendHexEscape(buffer, c);
      break;
    }
    span_start = i + 1;
  }

  if (span_start < size) {
    buffer.append(data + span_start, size - span_start);
  }
}

// Fallback for strings containing invalid UTF-8 sequences: emits 4-digit lowercase hex escapes
// (\u00XX) for control characters, quotes, backslashes, DEL (0x7f), and bytes >= 0x80.
void escapeInvalidUtf8(std::string& buffer, absl::string_view str) {
  buffer.clear();
  buffer.reserve(str.size() + 16);
  const char* const data = str.data();
  const size_t size = str.size();
  size_t span_start = 0;

  for (size_t i = 0; i < size; ++i) {
    const uint8_t c = static_cast<uint8_t>(data[i]);
    if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') {
      continue;
    }
    if (i > span_start) {
      buffer.append(data + span_start, i - span_start);
    }
    appendHexEscape(buffer, c);
    span_start = i + 1;
  }

  if (span_start < size) {
    buffer.append(data + span_start, size - span_start);
  }
}

} // namespace

absl::string_view sanitize(std::string& buffer, absl::string_view str) {
  // Fast scan to check whether any byte needs JSON escaping
  // (< 0x20, '"', '\\') or is non-ASCII (>= 0x80).
  uint8_t need_escape = 0;
  uint8_t or_all = 0;
  for (const char c : str) {
    const uint8_t uc = static_cast<uint8_t>(c);
    or_all |= uc;
    need_escape |= static_cast<uint8_t>((uc < 0x20) | (uc == '"') | (uc == '\\'));
  }

  const bool has_non_ascii = (or_all & 0x80) != 0;
  if (!has_non_ascii || utf8_range::IsStructurallyValid(str)) {
    if (!need_escape) {
      return str;
    }
    escapeValidUtf8(buffer, str);
    return buffer;
  }

  escapeInvalidUtf8(buffer, str);
  return buffer;
}

absl::string_view stripDoubleQuotes(absl::string_view str) {
  if (str.size() >= 2 && str[0] == '"' && str[str.size() - 1] == '"') {
    str = str.substr(1, str.size() - 2);
  } else {
    ASSERT(false,
           absl::StrCat("stripDoubleQuotes called on a str that lacks double-quotes: ", str));
  }
  return str;
}

} // namespace Json
} // namespace Envoy
