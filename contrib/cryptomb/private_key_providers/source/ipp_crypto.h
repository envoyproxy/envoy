#pragma once

#include "envoy/common/pure.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

class IppCrypto {
public:
  virtual ~IppCrypto() = default;

  virtual int mbxIsCryptoMbApplicable(uint64_t features) PURE;
  virtual uint32_t mbxNistp256EcdsaSignSslMb8(uint8_t* pa_sign_r[8], uint8_t* pa_sign_s[8],
                                              const uint8_t* const pa_msg[8],
                                              const BIGNUM* const pa_eph_skey[8],
                                              const BIGNUM* const pa_reg_skey[8]) PURE;
  virtual uint32_t mbxRsaPrivateCrtSslMb8(const uint8_t* const from_pa[8], uint8_t* const to_pa[8],
                                          const BIGNUM* const p_pa[8], const BIGNUM* const q_pa[8],
                                          const BIGNUM* const dp_pa[8],
                                          const BIGNUM* const dq_pa[8],
                                          const BIGNUM* const iq_pa[8],
                                          int expected_rsa_bitsize) PURE;
  virtual uint32_t mbxRsaPublicSslMb8(const uint8_t* const from_pa[8], uint8_t* const to_pa[8],
                                      const BIGNUM* const e_pa[8], const BIGNUM* const n_pa[8],
                                      int expected_rsa_bitsize) PURE;
  virtual bool mbxGetSts(uint32_t status, unsigned req_num) PURE;
};

using IppCryptoSharedPtr = std::shared_ptr<IppCrypto>;

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
