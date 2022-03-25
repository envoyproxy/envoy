#include "source/common/json/json_sanitizer.h"

#include <utility>

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {

namespace {

const uint8_t Literal = 0;
const uint8_t ControlEscapeSize = 2; // e.g. \b
const uint8_t UnicodeEscapeSize = 6; // e.g. \u1234
const uint8_t Utf8DecodeSentinel = 0xff;

} // namespace

JsonSanitizer::JsonSanitizer() {
  // Single-char escape sequences for common control characters.
  auto symbolic_escape = [this](char control_char, char symbolic) {
    Escape& escape = char_escapes_[char2uint32(control_char)];
    escape.size_ = ControlEscapeSize;
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

  // Low characters (0-31) not listed above are encoded as unicode 4-digit hex.
  auto unicode_escape = [this](uint32_t index) {
    unicode_escapes_.insert(index);
    if (index >= NumEscapes) {
      return;
    }
    Escape& escape = char_escapes_[index];
    std::string escape_str = absl::StrFormat("\\u%04x", index);
    ASSERT(escape_str.size() == UnicodeEscapeSize);
    escape.size_ = escape_str.size();
    RELEASE_ASSERT(escape.size_ <= sizeof(escape.chars_), "escaped string too large");
    memcpy(escape.chars_, escape_str.data(), escape_str.size()); // NOLINT(safe-memcpy)*/
  };

  for (char ch : {'\0', '<', '>', '\177'}) {
    unicode_escape(char2uint32(ch));
  }

  // Add unicode escapes for control-characters below 32 that don't have symbolic escapes.
  for (uint32_t i = 0; i < ' '; ++i) {
    if (char_escapes_[i].size_ == 0) {
      unicode_escape(i);
    }
  }

  // There's a range of low-numbered 8-bit unicode characters that are unicode escaped
  // by the protobuf library, so we match behavior.
  for (uint32_t i = 0x0080; i < 0x00a0; ++i) {
    unicode_escape(i);
  }

  // Most high numbered unicode characters are passed through literally.
  for (uint32_t i = 0x00a0; i < NumEscapes; ++i) {
    char_escapes_[i].size_ = Literal;
  }

  // However all the bytes matching pattern 11xxxxxx will be evaluated as utf-8.
  for (uint32_t i = Utf8_2BytePattern; i < 0xff; ++i) {
    if (char_escapes_[i].size_ == UnicodeEscapeSize) {
      unicode_escapes_.insert(i);
    }
    char_escapes_[i].size_ = Utf8DecodeSentinel;
  }

  // There are a few high numbered unicode characters that protobufs quote, so
  // we do likewise here to make differential testing/fuzzing easier.
  for (uint32_t i : {0x00ad, 0x0600, 0x0601, 0x0602, 0x0603, 0x06dd, 0x070f}) {
    unicode_escape(i);
  }


}

absl::string_view JsonSanitizer::sanitize(std::string& buffer, absl::string_view str) const {
  // Fast-path to see whether any escapes or utf-encoding are needed.
  uint32_t size_total = 0;
  for (uint32_t i = 0, n = str.size(); i < n; ++i) {
    size_total += char_escapes_[char2uint32(str[i])].size_;
  }
  if (size_total == 0) {
    return str;
  }
  return slowSanitize(buffer, str);
}

absl::string_view JsonSanitizer::slowSanitize(std::string& buffer, absl::string_view str) const {
  char hex_escape_buf[] = "\\u0000";
  static constexpr char hex_chars[] = "0123456789abcdef";
  size_t past_escape = absl::string_view::npos;
  const uint8_t* first = reinterpret_cast<const uint8_t*>(str.data());
  const uint8_t* data = first;
  absl::string_view escape_view;
  for (uint32_t n = str.size(); n != 0; ++data, --n) {
    const Escape& escape = char_escapes_[*data];
    if (escape.size_ != Literal) {
      uint32_t start_of_escape = data - first;
      switch (escape.size_) {
        case ControlEscapeSize:
        case UnicodeEscapeSize:
          escape_view = absl::string_view(escape.chars_, escape.size_);
          break;
        case Utf8DecodeSentinel: {
          auto [unicode, consumed] = decodeUtf8(data, n);
          if (consumed != 0) {
            --consumed;
            data += consumed;
            n -= consumed;
            //if (unicode >= NumEscapes) {
            //  continue; // 3-byte and 4-byte utf-8 code-points are not in table.
            //}

            // We are indexing into char_escapes_ two ways: by the original first
            // byte of a utf-8 sequence, and then, if the size_ is non-zero, we
            // decode the utf8 sequence and do another lookup into the array using
            // the resolved unicode. This is OK because in the first lookup, the
            // utf8 intro pattern lands us between 128 and 255, which will all be
            // initialized in the constructor with non-zero size. Then if we decode
            // we will find the correct Escape entry for the unicode.
            if (unicode_escapes_.find(unicode) != unicode_escapes_.end()) {
              hex_escape_buf[2] = hex_chars[(unicode >> 12) & 0xf];
              hex_escape_buf[3] = hex_chars[(unicode >> 8) & 0xf];
              hex_escape_buf[4] = hex_chars[(unicode >> 4) & 0xf];
              hex_escape_buf[5] = hex_chars[unicode & 0xf];
              escape_view = absl::string_view(hex_escape_buf, UnicodeEscapeSize);
            } else {
              continue;
            }

            /*            escape = &char_escapes_[unicode];
            if (escape.size_ == Literal) {
              continue; // Most code-points are not escaped.
            }
            escape_view = absl::string_view(escape.chars_, escape.size_);
            */
          } else {
            hex_escape_buf[2] = '0';
            hex_escape_buf[3] = '0';
            hex_escape_buf[4] = hex_chars[*data >> 4];
            hex_escape_buf[5] = hex_chars[*data & 0xf];
            escape_view = absl::string_view(hex_escape_buf, UnicodeEscapeSize);
          }
          break;
        }
        default:
          ASSERT(false);
      }

      if (past_escape == absl::string_view::npos) {
        // We only initialize buffer when we first learn we need to add an
        // escape-sequence to the sanitized string.
        if (start_of_escape == 0) {
          // The first character is an escape, and 'buffer' has not been cleared yet,
          // so we need to assign it rather than append to it.
          buffer.assign(escape_view.data(), escape_view.size());
        } else {
          // We found our first escape, but this is not the first character in the
          // string, so we combine the unescaped characters in the string we already
          // looped over with the new escaped character.
          buffer = absl::StrCat(str.substr(0, start_of_escape), escape_view);
        }
      } else if (start_of_escape == past_escape) {
        // We are adding an escape immediately after another escaped character.
        absl::StrAppend(&buffer, escape_view);
      } else {
        // We are adding a new escape but must first cover the characters
        // encountered since the previous escape.
        absl::StrAppend(&buffer, str.substr(past_escape, start_of_escape - past_escape),
                        escape_view);
      }
      past_escape = data - first + 1;
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

std::pair<uint32_t, uint32_t> JsonSanitizer::decodeUtf8(const uint8_t* bytes, uint32_t size) {
  uint32_t unicode = 0;
  uint32_t consumed = 0;

  // See table in https://en.wikipedia.org/wiki/UTF-8, "Encoding" section.
  //
  // See also https://en.cppreference.com/w/cpp/locale/codecvt_utf8 which is
  // marked as deprecated. There is also support in Windows libraries and Boost,
  // which can be discovered on StackOverflow. I could not find a usable OSS
  // implementation. However it's easily derived from the spec on Wikipedia.
  //
  // Note that the code below could be optimized a bit, e.g. by factoring out
  // repeated lookups of the same index in the bytes array and using SSE
  // instructions for the multi-word bit hacking.
  if (size >= 2 && (bytes[0] & Utf8_2ByteMask) == Utf8_2BytePattern &&
      (bytes[1] & Utf8_ContinueMask) == Utf8_ContinuePattern) {
    unicode = bytes[0] & ~Utf8_2ByteMask;
    unicode = (unicode << Utf8_Shift) | (bytes[1] & ~Utf8_ContinueMask);
    consumed = 2;
  } else if (size >= 3 && (bytes[0] & Utf8_3ByteMask) == Utf8_3BytePattern &&
             (bytes[1] & Utf8_ContinueMask) == Utf8_ContinuePattern &&
             (bytes[2] & Utf8_ContinueMask) == Utf8_ContinuePattern) {
    unicode = bytes[0] & ~Utf8_3ByteMask;
    unicode = (unicode << Utf8_Shift) | (bytes[1] & ~Utf8_ContinueMask);
    unicode = (unicode << Utf8_Shift) | (bytes[2] & ~Utf8_ContinueMask);
    consumed = 3;
  } else if (size >= 4 && (bytes[0] & Utf8_4ByteMask) == Utf8_4BytePattern &&
             (bytes[1] & Utf8_ContinueMask) == Utf8_ContinuePattern &&
             (bytes[2] & Utf8_ContinueMask) == Utf8_ContinuePattern &&
             (bytes[3] & Utf8_ContinueMask) == Utf8_ContinuePattern) {
    unicode = bytes[0] & ~Utf8_4ByteMask;
    unicode = (unicode << Utf8_Shift) | (bytes[1] & ~Utf8_ContinueMask);
    unicode = (unicode << Utf8_Shift) | (bytes[2] & ~Utf8_ContinueMask);
    unicode = (unicode << Utf8_Shift) | (bytes[3] & ~Utf8_ContinueMask);
    consumed = 4;
  }
  return UnicodeSizePair(unicode, consumed);
}

} // namespace Json
} // namespace Envoy
