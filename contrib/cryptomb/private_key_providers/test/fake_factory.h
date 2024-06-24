#pragma once

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#include "contrib/cryptomb/private_key_providers/source/ipp_crypto.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

class FakeIppCryptoImpl : public virtual IppCrypto {
public:
  FakeIppCryptoImpl(bool supported_instruction_set);

  int mbxIsCryptoMbApplicable(uint64_t features) override;
  uint32_t mbxNistp256EcdsaSignSslMb8(uint8_t* pa_sign_r[8], uint8_t* pa_sign_s[8],
                                      const uint8_t* const pa_msg[8],
                                      const BIGNUM* const pa_eph_skey[8],
                                      const BIGNUM* const pa_reg_skey[8]) override;
  uint32_t mbxRsaPrivateCrtSslMb8(const uint8_t* const from_pa[8], uint8_t* const to_pa[8],
                                  const BIGNUM* const p_pa[8], const BIGNUM* const q_pa[8],
                                  const BIGNUM* const dp_pa[8], const BIGNUM* const dq_pa[8],
                                  const BIGNUM* const iq_pa[8], int expected_rsa_bitsize) override;
  uint32_t mbxRsaPublicSslMb8(const uint8_t* const from_pa[8], uint8_t* const to_pa[8],
                              const BIGNUM* const e_pa[8], const BIGNUM* const n_pa[8],
                              int expected_rsa_bitsize) override;
  bool mbxGetSts(uint32_t status, unsigned req_num) override;

  void setRsaKey(RSA* rsa) { RSA_get0_key(rsa, &n_, &e_, &d_); };

  void injectErrors(bool enabled) { inject_errors_ = enabled; }

private:
  uint32_t mbxSetSts(uint32_t status, unsigned req_num, bool success);

  bool supported_instruction_set_;
  const BIGNUM* n_{};
  const BIGNUM* e_{};
  const BIGNUM* d_{};

  bool inject_errors_{};
};

class FakeCryptoMbPrivateKeyMethodFactory : public Ssl::PrivateKeyMethodProviderInstanceFactory {
public:
  FakeCryptoMbPrivateKeyMethodFactory(bool supported_instruction_set);

  // Ssl::PrivateKeyMethodProviderInstanceFactory
  Ssl::PrivateKeyMethodProviderSharedPtr createPrivateKeyMethodProviderInstance(
      const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& message,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) override;
  std::string name() const override { return "cryptomb"; };

private:
  bool supported_instruction_set_;
};

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
