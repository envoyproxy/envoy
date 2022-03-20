#include "source/common/json/json_sanitizer.h"

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {

JsonSanitizer::JsonSanitizer() {
  // Single-char escape sequences for common control characters.
  auto symbolic_escape = [this](char escape_char, char symbolic) {
    Escape& escape = char_escapes_[static_cast<uint32_t>(escape_char)];
    escape.size_ = 2;
    escape.chars_[0] = '\\';
    escape.chars_[1] = symbolic;
  };
  symbolic_escape('\b', 'b');
  symbolic_escape('\f', 'f');
  symbolic_escape('\n', 'n');
  symbolic_escape('\r', 'r');
  symbolic_escape('\t', 't');
  symbolic_escape('\\', '\\');
  symbolic_escape('"', '"');

  // Low characters (0-31) not listed above are encoded as unicode 4-digit hex, plus
  // a few other specific ones.
  auto unicode_escape = [this](uint32_t index) {
    Escape& escape = char_escapes_[static_cast<uint32_t>(index)];
    if (escape.size_ == 0) {
      std::string escape_str = absl::StrFormat("\\u%04x", index);
      escape.size_ = escape_str.size();
      RELEASE_ASSERT(escape.size_ <= sizeof(escape.chars_), "escaped string too large");
      memcpy(escape.chars_, escape_str.data(), escape_str.size()); // NOLINT(safe-memcpy)
    }
  };
  unicode_escape('<');
  unicode_escape('>');
  unicode_escape(127);

  // Control-characters below 32.
  for (uint32_t i = 0; i < ' '; ++i) {
    unicode_escape(i);
  }
}

absl::string_view JsonSanitizer::sanitize(std::string& buffer, absl::string_view str) const {
  size_t past_escape = absl::string_view::npos;
  for (uint32_t i = 0, n = str.size(); i < n; ++i) {
    uint32_t index = static_cast<uint32_t>(static_cast<uint8_t>(str[i]));
    const Escape& escape = char_escapes_[index];
    if (escape.size_ != 0) {
      absl::string_view escape_view(escape.chars_, escape.size_);
      if (past_escape == absl::string_view::npos) {
        // We only initialize buffer when we first learn we need to add an
        // escape-sequence to the sanitized string.
        if (i == 0) {
          // The first character is an escape, and 'buffer' has not been cleared yet,
          // so we need to assign it rather than append to it.
          buffer.assign(escape_view.data(), escape_view.size());
        } else {
          // We found our first escape, but this is not the first character in the
          // string, so we combine the unescaped characters in the string we already
          // looped over with the new escaped character.
          buffer = absl::StrCat(str.substr(0, i), escape_view);
        }
      } else if (i == past_escape) {
        // We are adding an escape immediately after another escaped character.
        absl::StrAppend(&buffer, escape_view);
      } else {
        // We are adding a new escape but must first cover the characters
        // encountered since the previous escape.
        absl::StrAppend(&buffer, str.substr(past_escape, i - past_escape), escape_view);
      }
      past_escape = i + 1;
    }
  }

  // If no escape-sequence was needed, we just return the input.
  if (past_escape == absl::string_view::npos) {
    return str;
  }

  // Otherwise we append on any unescaped chunk at the end of the input, and
  // return buffer as the result.
  if (past_escape < str.size()) {
    absl::StrAppend(&buffer, str.substr(past_escape, str.size() - past_escape));
  }
  return buffer;
}

} // namespace Json
} // namespace Envoy
