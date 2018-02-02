#include "common/common/base64.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

void Runner::execute(const uint8_t* buf, size_t len) {
  Envoy::Base64::encode(reinterpret_cast<const char*>(buf), len);
}

} // namespace Fuzz
} // namespace Envoy
