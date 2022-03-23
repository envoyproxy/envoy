#include "source/common/json/json_sanitizer.h"

#include <utility>

#include "source/common/common/assert.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"

namespace Envoy {
namespace Json {

namespace {

const uint8_t Utf8PassThroughSentinel = 0xff;

} // namespace

JsonSanitizer::JsonSanitizer() {
  // Single-char escape sequences for common control characters.
  auto symbolic_escape = [this](char control_char, char symbolic) {
    Escape& escape = char_escapes_[char2uint32(control_char)];
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

  // Low characters (0-31) not listed above are encoded as unicode 4-digit hex.
  auto unicode_escape = [this](uint32_t index) {
    Escape& escape = char_escapes_[index];
    std::string escape_str = absl::StrFormat("\\u%04x", index);
    escape.size_ = escape_str.size();
    RELEASE_ASSERT(escape.size_ <= sizeof(escape.chars_), "escaped string too large");
    memcpy(escape.chars_, escape_str.data(), escape_str.size()); // NOLINT(safe-memcpy)
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
  for (uint32_t i = 0x00a0; i <= NumEscapes; ++i) {
    char_escapes_[i].size_ = Utf8PassThroughSentinel;
  }

  // There are a few high numbered unicode characters that protobufs quote, so
  // we do likewise here to make differential testing/fuzzing easier.
  for (uint32_t i : {0x00ad, 0x0600, 0x0601, 0x0602, 0x0603, 0x06dd, 0x070f}) {
    unicode_escape(i);
  }
}

absl::string_view JsonSanitizer::sanitize(std::string& buffer, absl::string_view str) const {
  size_t past_escape = absl::string_view::npos;
  const uint8_t* first = reinterpret_cast<const uint8_t*>(str.data());
  const uint8_t* data = first;
  for (uint32_t n = str.size(); n != 0; ++data, --n) {
    const Escape* escape = &char_escapes_[*data];
    if (escape->size_ != 0) {
      uint32_t start_of_escape = data - first;
      absl::string_view escape_view;
      auto [unicode, consumed] = decodeUtf8(data, n);
      if (consumed != 0) {
        --consumed;
        data += consumed;
        n -= consumed;
        if (unicode >= NumEscapes) {
          continue; // 3-byte and 4-byte utf-8 code-points are not in table.
        }
        escape = &char_escapes_[unicode];
        if (escape->size_ == Utf8PassThroughSentinel) {
          continue; // Most code-points are not escaped.
        }
      }
      escape_view = absl::string_view(escape->chars_, escape->size_);

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

bool JsonSanitizer::isValidUtf8(absl::string_view in) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(in.data());
  uint32_t size = in.size();
  while (size != 0) {
    if ((*data & 0x80) == 0) {
      ++data;
      --size;
    } else {
      auto [unicode, consumed] = Envoy::Json::JsonSanitizer::decodeUtf8(data, size);
      if (consumed == 0) {
        return false;
      }
      data += consumed;
      size -= consumed;
    }
  }
  return true;
}

} // namespace Json
} // namespace Envoy
