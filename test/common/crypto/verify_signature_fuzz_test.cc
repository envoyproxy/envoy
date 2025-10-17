#include "source/common/common/hex.h"
#include "source/common/crypto/utility.h"

#include "test/common/crypto/verify_signature_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"

#include "absl/types/span.h"

namespace Envoy {
namespace Common {
namespace Crypto {
namespace {

DEFINE_PROTO_FUZZER(const test::common::crypto::VerifySignatureFuzzTestCase& input) {
  const auto& key = input.key();
  const auto& hash_func = input.hash_func();
  const auto& signature = input.signature();
  const auto& data = input.data();

  auto key_vec = Hex::decode(key);
  Common::Crypto::PKeyObjectPtr crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPublicKeyDER(key_vec));
  Common::Crypto::PKeyObject* crypto(crypto_ptr.get());

  std::vector<uint8_t> text(data.begin(), data.end());

  const auto sig = Hex::decode(signature);
  auto result = UtilitySingleton::get().verifySignature(hash_func, *crypto, sig, text);
  // Ignore the result for fuzzing purposes - we're just testing that it doesn't crash
  (void)result;
}

} // namespace
} // namespace Crypto
} // namespace Common
} // namespace Envoy
