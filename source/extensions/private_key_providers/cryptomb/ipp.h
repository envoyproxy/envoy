#pragma once

#include "crypto_mb/cpu_features.h"
#include "crypto_mb/ec_nistp256.h"
#include "crypto_mb/rsa.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {

class IppCrypto {
public:
  virtual ~IppCrypto() = default;

  virtual int mbx_is_crypto_mb_applicable(int64u features) PURE;
  virtual mbx_status mbx_nistp256_ecdsa_sign_ssl_mb8(int8u* pa_sign_r[8], int8u* pa_sign_s[8],
                                                     const int8u* const pa_msg[8],
                                                     const BIGNUM* const pa_eph_skey[8],
                                                     const BIGNUM* const pa_reg_skey[8],
                                                     int8u* pBuffer) PURE;
  virtual mbx_status
  mbx_rsa_private_crt_ssl_mb8(const int8u* const from_pa[8], int8u* const to_pa[8],
                              const BIGNUM* const p_pa[8], const BIGNUM* const q_pa[8],
                              const BIGNUM* const dp_pa[8], const BIGNUM* const dq_pa[8],
                              const BIGNUM* const iq_pa[8], int expected_rsa_bitsize) PURE;
  virtual mbx_status mbx_rsa_public_ssl_mb8(const int8u* const from_pa[8], int8u* const to_pa[8],
                                            const BIGNUM* const e_pa[8],
                                            const BIGNUM* const n_pa[8],
                                            int expected_rsa_bitsize) PURE;
};

class IppCryptoImpl : public virtual IppCrypto {
public:
  int mbx_is_crypto_mb_applicable(int64u features) override {
    return ::mbx_is_crypto_mb_applicable(features);
  }
  mbx_status mbx_nistp256_ecdsa_sign_ssl_mb8(int8u* pa_sign_r[8], int8u* pa_sign_s[8],
                                             const int8u* const pa_msg[8],
                                             const BIGNUM* const pa_eph_skey[8],
                                             const BIGNUM* const pa_reg_skey[8],
                                             int8u* pBuffer) override {
    return ::mbx_nistp256_ecdsa_sign_ssl_mb8(pa_sign_r, pa_sign_s, pa_msg, pa_eph_skey, pa_reg_skey,
                                             pBuffer);
  }
  mbx_status mbx_rsa_private_crt_ssl_mb8(const int8u* const from_pa[8], int8u* const to_pa[8],
                                         const BIGNUM* const p_pa[8], const BIGNUM* const q_pa[8],
                                         const BIGNUM* const dp_pa[8], const BIGNUM* const dq_pa[8],
                                         const BIGNUM* const iq_pa[8],
                                         int expected_rsa_bitsize) override {
    return ::mbx_rsa_private_crt_ssl_mb8(from_pa, to_pa, p_pa, q_pa, dp_pa, dq_pa, iq_pa,
                                         expected_rsa_bitsize);
  }
  mbx_status mbx_rsa_public_ssl_mb8(const int8u* const from_pa[8], int8u* const to_pa[8],
                                    const BIGNUM* const e_pa[8], const BIGNUM* const n_pa[8],
                                    int expected_rsa_bitsize) override {
    return ::mbx_rsa_public_ssl_mb8(from_pa, to_pa, e_pa, n_pa, expected_rsa_bitsize);
  }
};

using IppCryptoSharedPtr = std::shared_ptr<IppCrypto>;

} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
