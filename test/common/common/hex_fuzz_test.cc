#include "common/common/hex.h"

#include "test/fuzz/fuzz_runner.h"
#include "test/fuzz/utility.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  Envoy::Hex::encode(buf, len);
  Envoy::Hex::decode(std::string(reinterpret_cast<const char*>(buf), len));

  FuzzedDataProvider fuzz_data(buf, len);
  auto decoded = Envoy::Hex::decode(Envoy::Hex::encode(buf, len));
  FUZZ_ASSERT(fuzz_data.ConsumeBytes<uint8_t>(len) == decoded);

  Envoy::Hex::uint64ToHex(fuzz_data.ConsumeIntegral<uint64_t>());
  Envoy::Hex::uint32ToHex(fuzz_data.ConsumeIntegral<uint32_t>());
}

} // namespace Fuzz
} // namespace Envoy