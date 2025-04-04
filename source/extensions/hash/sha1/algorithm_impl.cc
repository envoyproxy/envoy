#include "source/extensions/hash/sha1/algorithm_impl.h"

#include <openssl/sha.h>

namespace Envoy {
namespace Extensions {
namespace Hash {

std::string SHA1AlgorithmImpl::computeHash(absl::string_view input) {
  unsigned char hash[SHA_DIGEST_LENGTH];
  SHA1(reinterpret_cast<const unsigned char*>(input.data()), input.length(), hash);
  std::string result(reinterpret_cast<const char*>(hash), SHA_DIGEST_LENGTH);
  return result;
}

uint32_t SHA1AlgorithmImpl::digestLength() { return SHA_DIGEST_LENGTH; }

// The base64 encoded SHA1 hash is 28 bytes long
uint32_t SHA1AlgorithmImpl::base64EncodedHashLength() { return 28; }

} // namespace Hash
} // namespace Extensions
} // namespace Envoy
