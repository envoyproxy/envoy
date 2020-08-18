#include "common/common/base64.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/escaping.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const std::string str(reinterpret_cast<const char*>(buf), len);
  std::string decoded, web_safe_decoded;
  absl::Base64Escape(str);
  absl::Base64Unescape(str, &decoded);
  absl::WebSafeBase64Escape(str);
  absl::WebSafeBase64Unescape(str, &web_safe_decoded);
  // Envoy::Base64::encode(reinterpret_cast<const char*>(buf), len);
  // Envoy::Base64::decode(std::string(reinterpret_cast<const char*>(buf), len));
  // Envoy::Base64Url::encode(reinterpret_cast<const char*>(buf), len);
  // Envoy::Base64Url::decode(std::string(reinterpret_cast<const char*>(buf), len));
}

} // namespace Fuzz
} // namespace Envoy
