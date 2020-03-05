#include "common/common/hex.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  Envoy::Hex::encode(buf, len);
  Envoy::Hex::decode(std::string(reinterpret_cast<const char*>(buf), len));
}

} // namespace Fuzz
} // namespace Envoy