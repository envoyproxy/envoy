#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/assert.h"
#include "source/common/crypto/utility.h"

#include "test/fuzz/fuzz_runner.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Fuzz {

DEFINE_FUZZER(const uint8_t* buf, size_t len) {
  Buffer::OwnedImpl buffer(buf, len);
  auto digest = Common::Crypto::UtilitySingleton::get().getSha256Digest(buffer);
  auto digest_from_string_view = Common::Crypto::UtilitySingleton::get().getSha256Digest(
      absl::string_view(reinterpret_cast<const char*>(buf), len));
  RELEASE_ASSERT(digest == digest_from_string_view, "string_view and buffer digests must match");
}

} // namespace Fuzz
} // namespace Envoy
