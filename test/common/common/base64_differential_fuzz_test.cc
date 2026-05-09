#include "source/common/common/base64.h"

#include "test/common/common/base64_legacy.h"
#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  const std::string input(reinterpret_cast<const char*>(buf), len);

  // Encode: both must produce identical output for any input
  FUZZ_ASSERT(Base64::encode(input) == Base64Legacy::encode(input));
  FUZZ_ASSERT(Base64Url::encode(input.data(), input.size()) ==
              Base64UrlLegacy::encode(input.data(), input.size()));

  // Decode: both must agree on every input, valid or invalid
  FUZZ_ASSERT(Base64::decode(input) == Base64Legacy::decode(input));
  FUZZ_ASSERT(Base64Url::decode(input) == Base64UrlLegacy::decode(input));
}

} // namespace Fuzz
} // namespace Envoy
