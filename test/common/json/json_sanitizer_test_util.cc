#include "test/common/json/json_sanitizer_test_util.h"

#include <string>

#include "source/common/json/json_sanitizer.h"

namespace Envoy {
namespace Json {

absl::string_view stripDoubleQuotes(absl::string_view str) {
  if (str.size() >= 2 && str[0] == '"' && str[str.size() - 1] == '"') {
    return str.substr(1, str.size() - 2);
  }
  return str;
}

bool isValidUtf8(absl::string_view in) {
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
