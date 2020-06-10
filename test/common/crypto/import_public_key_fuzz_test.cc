#include "common/buffer/buffer_impl.h"
#include "common/crypto/utility.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  std::vector<uint8_t> key(buf, buf + len);
  auto digest = Common::Crypto::UtilitySingleton::get().importPublicKey(key);
}

} // namespace Fuzz
} // namespace Envoy
