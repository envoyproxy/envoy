#pragma once

#include "source/common/common/logger.h"
#include "source/extensions/common/aws/signers/sigv4a_common.h"

namespace Envoy {
namespace Extensions {
namespace Common {
namespace Aws {

class SigV4AKeyDerivationBase {
public:
  virtual ~SigV4AKeyDerivationBase() = default;
  virtual absl::StatusOr<EC_KEY*> derivePrivateKey(absl::string_view access_key_id,
                                                   absl::string_view secret_access_key) PURE;
  virtual bool derivePublicKey(EC_KEY* ec_key) PURE;
};

class SigV4AKeyDerivation : public SigV4AKeyDerivationBase,
                            public Logger::Loggable<Logger::Id::aws> {
public:
  absl::StatusOr<EC_KEY*> derivePrivateKey(absl::string_view access_key_id,
                                           absl::string_view secret_access_key) override;
  bool derivePublicKey(EC_KEY* ec_key) override;

private:
  bool constantTimeLessThanOrEqualTo(std::vector<uint8_t> lhs_raw_be_bigint,
                                     std::vector<uint8_t> rhs_raw_be_bigint);
  void constantTimeAddOne(std::vector<uint8_t>* raw_be_bigint);
};

} // namespace Aws
} // namespace Common
} // namespace Extensions
} // namespace Envoy
