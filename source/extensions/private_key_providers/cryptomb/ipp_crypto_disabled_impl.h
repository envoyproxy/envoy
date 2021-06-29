#pragma once

#include "source/extensions/private_key_providers/cryptomb/ipp_crypto.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

class IppCryptoImpl : public virtual IppCrypto {
public:
  int mbxIsCryptoMbApplicable(uint64_t features) override {
    UNREFERENCED_PARAMETER(features);
    return 0;
  }
  uint32_t mbxNistp256EcdsaSignSslMb8(uint8_t* pa_sign_r[8], uint8_t* pa_sign_s[8],
                                      const uint8_t* const pa_msg[8],
                                      const BIGNUM* const pa_eph_skey[8],
                                      const BIGNUM* const pa_reg_skey[8],
                                      uint8_t* p_buffer) override {
    UNREFERENCED_PARAMETER(pa_sign_r);
    UNREFERENCED_PARAMETER(pa_sign_s);
    UNREFERENCED_PARAMETER(pa_msg);
    UNREFERENCED_PARAMETER(pa_eph_skey);
    UNREFERENCED_PARAMETER(pa_reg_skey);
    UNREFERENCED_PARAMETER(p_buffer);
    return 0xff;
  }
  uint32_t mbxRsaPrivateCrtSslMb8(const uint8_t* const from_pa[8], uint8_t* const to_pa[8],
                                  const BIGNUM* const p_pa[8], const BIGNUM* const q_pa[8],
                                  const BIGNUM* const dp_pa[8], const BIGNUM* const dq_pa[8],
                                  const BIGNUM* const iq_pa[8], int expected_rsa_bitsize) override {
    UNREFERENCED_PARAMETER(from_pa);
    UNREFERENCED_PARAMETER(to_pa);
    UNREFERENCED_PARAMETER(p_pa);
    UNREFERENCED_PARAMETER(q_pa);
    UNREFERENCED_PARAMETER(dp_pa);
    UNREFERENCED_PARAMETER(dq_pa);
    UNREFERENCED_PARAMETER(iq_pa);
    UNREFERENCED_PARAMETER(expected_rsa_bitsize);
    return 0xff;
  }
  uint32_t mbxRsaPublicSslMb8(const uint8_t* const from_pa[8], uint8_t* const to_pa[8],
                              const BIGNUM* const e_pa[8], const BIGNUM* const n_pa[8],
                              int expected_rsa_bitsize) override {
    UNREFERENCED_PARAMETER(from_pa);
    UNREFERENCED_PARAMETER(to_pa);
    UNREFERENCED_PARAMETER(e_pa);
    UNREFERENCED_PARAMETER(n_pa);
    UNREFERENCED_PARAMETER(expected_rsa_bitsize);
    return 0xff;
  }
  bool mbxGetSts(uint32_t status, unsigned req_num) override {
    UNREFERENCED_PARAMETER(status);
    UNREFERENCED_PARAMETER(req_num);
    return false;
  };
};

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy