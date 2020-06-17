#include "common/common/hex.h"
#include "common/crypto/utility.h"

#include "test/common/crypto/verify_signature_fuzz.pb.validate.h"
#include "test/fuzz/fuzz_runner.h"

namespace Envoy {
namespace Common {
namespace Crypto {
namespace {

DEFINE_PROTO_FUZZER(const test::common::crypto::VerifySignatureFuzzTestCase& input) {
  auto const& key = input.key();
  auto const& hash_func = input.hash_func();
  auto const& signature = input.signature();
  auto const& data = input.data();

  Common::Crypto::CryptoObjectPtr crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPublicKey(Hex::decode(key)));
  Common::Crypto::CryptoObject* crypto(crypto_ptr.get());

  std::vector<uint8_t> text(data.begin(), data.end());

  auto sig = Hex::decode(signature);
  auto result = UtilitySingleton::get().verifySignature(hash_func, *crypto, sig, text);
}

} // namespace
} // namespace Crypto
} // namespace Common
} // namespace Envoy
