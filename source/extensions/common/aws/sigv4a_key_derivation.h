#pragma once

#include "source/extensions/common/aws/sigv4a_signer_impl.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class SigV4AKeyDerivation : public Logger::Loggable<Logger::Id::aws> {
public:
  static EC_KEY* derivePrivateKey(absl::string_view access_key_id,
                                  absl::string_view secret_access_key);
  static bool derivePublicKey(EC_KEY* ec_key);

private:
  static bool constantTimeLessThanOrEqualTo(std::vector<uint8_t> lhs_raw_be_bigint,
                                            std::vector<uint8_t> rhs_raw_be_bigint);
  static void constantTimeAddOne(std::vector<uint8_t>* raw_be_bigint);
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
