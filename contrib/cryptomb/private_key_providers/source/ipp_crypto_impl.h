#pragma once

#include "contrib/cryptomb/private_key_providers/source/ipp_crypto.h"
#include "crypto_mb/cpu_features.h"
#include "crypto_mb/ec_nistp256.h"
#include "crypto_mb/rsa.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

class IppCryptoImpl : public virtual IppCrypto {
public:
  int mbxIsCryptoMbApplicable(uint64_t features) override {
    return ::mbx_is_crypto_mb_applicable(features);
  }
  uint32_t mbxRsaPrivateCrtSslMb8(const uint8_t* const from_pa[8], uint8_t* const to_pa[8],
                                  const BIGNUM* const p_pa[8], const BIGNUM* const q_pa[8],
                                  const BIGNUM* const dp_pa[8], const BIGNUM* const dq_pa[8],
                                  const BIGNUM* const iq_pa[8], int expected_rsa_bitsize) override {
    return ::mbx_rsa_private_crt_ssl_mb8(from_pa, to_pa, p_pa, q_pa, dp_pa, dq_pa, iq_pa,
                                         expected_rsa_bitsize);
  }
  uint32_t mbxRsaPublicSslMb8(const uint8_t* const from_pa[8], uint8_t* const to_pa[8],
                              const BIGNUM* const e_pa[8], const BIGNUM* const n_pa[8],
                              int expected_rsa_bitsize) override {
    return ::mbx_rsa_public_ssl_mb8(from_pa, to_pa, e_pa, n_pa, expected_rsa_bitsize);
  }
  bool mbxGetSts(uint32_t status, unsigned req_num) override {
    if (MBX_GET_STS(status, req_num) == MBX_STATUS_OK) {
      return true;
    }
    return false;
  };
};

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
