#include "source/common/common/base64.h"

#include <cstdint>
#include <string>

#include "source/common/common/assert.h"
#include "source/common/common/empty_string.h"

#include "absl/container/fixed_array.h"
#include "absl/strings/escaping.h"
#include "absl/strings/str_cat.h"

namespace Envoy {

std::string Base64::decode(absl::string_view input) {
  std::string dest;
  absl::Base64Unescape(input, &dest);
  return dest;
}

std::string Base64::decodeWithoutPadding(absl::string_view input) {
  std::string dest;
  absl::Base64Unescape(input, &dest);
  return dest;
}

std::string Base64::encode(const Buffer::Instance& buffer, uint64_t length) {
  uint64_t output_length = (std::min(length, buffer.length()) + 2) / 3 * 4;
  std::string ret;
  ret.reserve(output_length);
  std::string buffer_str = buffer.toString();
  absl::string_view buffer_str_view(buffer_str);
  absl::Base64Escape(buffer_str_view, &ret);
  return ret;
}

std::string Base64::encode(const char* input, uint64_t length) {
  absl::string_view input_str_view(input, length);
  std::string dest;
  absl::Base64Escape(input_str_view, &dest);
  return dest;
}

std::string Base64::encode(const char* input, uint64_t length, bool add_padding) {
  absl::string_view input_str_view(input, length);
  std::string dest;
  absl::Base64Escape(input_str_view, &dest);
  if (!add_padding) {
    while (dest.back() == '=') {
      dest.pop_back();
    }
  }
  return dest;
}

void Base64::completePadding(std::string& encoded) {
  if (encoded.length() % 4 != 0) {
    std::string trailing_padding(4 - encoded.length() % 4, '=');
    absl::StrAppend(&encoded, trailing_padding);
  }
}

std::string Base64Url::decode(absl::string_view input) {
  std::string dest;
  absl::WebSafeBase64Unescape(input, &dest);
  return dest;
}

std::string Base64Url::encode(const char* input, uint64_t length) {
  absl::string_view input_str_view(input, length);
  std::string dest;
  absl::WebSafeBase64Escape(input_str_view, &dest);
  return dest;
}

} // namespace Envoy
