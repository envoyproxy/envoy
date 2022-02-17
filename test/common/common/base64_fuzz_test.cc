#include "source/common/common/base64.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  Envoy::Base64::encode(reinterpret_cast<const char*>(buf), len);
  Envoy::Base64::decode(std::string(reinterpret_cast<const char*>(buf), len));
  Envoy::Base64Url::encode(reinterpret_cast<const char*>(buf), len);
  Envoy::Base64Url::decode(std::string(reinterpret_cast<const char*>(buf), len));
}

} // namespace Fuzz
} // namespace Envoy
