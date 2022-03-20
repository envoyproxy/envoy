#include "source/common/json/json_sanitizer.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {

JsonSanitizer::JsonSanitizer() {
  // Single-char escape sequences for common control characters.
  auto control_char = [this](char escape, absl::string_view letter) {
    char_escapes_[static_cast<uint32_t>(escape)] =
        std::make_unique<std::string>(absl::StrCat("\\", letter));
  };
  control_char('\b', "b");
  control_char('\f', "f");
  control_char('\n', "n");
  control_char('\r', "r");
  control_char('\t', "t");
  control_char('\\', "\\");
  control_char('\"', "\"");

  // Low characters (0-31) not listed above are encoded as unicode 4-digit hex, plus
  // a few other specific ones.
  auto unicode_escape = [this](uint32_t index) {
    if (char_escapes_[index] == nullptr) {
      char_escapes_[index] = std::make_unique<std::string>(absl::StrFormat("\\u%04x", index));
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
    const std::string* escape = char_escapes_[index].get();
    if (escape != nullptr) {
      if (past_escape == absl::string_view::npos) {
        // We only initialize buffer when we first learn we need to add an
        // escape-sequence to the sanitized string.
        if (i == 0) {
          buffer = *escape;
        } else {
          buffer = absl::StrCat(str.substr(0, i), *escape);
        }
      } else if (i == past_escape) {
        absl::StrAppend(&buffer, *escape);
      } else {
        absl::StrAppend(&buffer, str.substr(past_escape, i - past_escape), *escape);
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
