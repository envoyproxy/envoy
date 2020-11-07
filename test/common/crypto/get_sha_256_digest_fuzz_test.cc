#include "common/buffer/buffer_impl.h"
#include "common/crypto/utility.h"

#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  Buffer::OwnedImpl buffer(buf, len);
  auto digest = Common::Crypto::UtilitySingleton::get().getSha256Digest(buffer);
}

} // namespace Fuzz
} // namespace Envoy
