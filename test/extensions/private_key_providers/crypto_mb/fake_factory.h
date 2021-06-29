#pragma once

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#include "source/extensions/private_key_providers/cryptomb/cryptomb_private_key_provider.h"
#include "source/extensions/private_key_providers/cryptomb/ipp_crypto.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

class FakeIppCryptoImpl : public virtual IppCrypto {
public:
  FakeIppCryptoImpl(bool supported_instruction_set);
  ~FakeIppCryptoImpl() override;

  int mbxIsCryptoMbApplicable(uint64_t features) override;
  uint32_t mbxNistp256EcdsaSignSslMb8(uint8_t* pa_sign_r[8], uint8_t* pa_sign_s[8],
                                      const uint8_t* const pa_msg[8],
                                      const BIGNUM* const pa_eph_skey[8],
                                      const BIGNUM* const pa_reg_skey[8],
                                      uint8_t* p_buffer) override;
  uint32_t mbxRsaPrivateCrtSslMb8(const uint8_t* const from_pa[8], uint8_t* const to_pa[8],
                                  const BIGNUM* const p_pa[8], const BIGNUM* const q_pa[8],
                                  const BIGNUM* const dp_pa[8], const BIGNUM* const dq_pa[8],
                                  const BIGNUM* const iq_pa[8], int expected_rsa_bitsize) override;
  uint32_t mbxRsaPublicSslMb8(const uint8_t* const from_pa[8], uint8_t* const to_pa[8],
                              const BIGNUM* const e_pa[8], const BIGNUM* const n_pa[8],
                              int expected_rsa_bitsize) override;
  bool mbxGetSts(uint32_t status, unsigned req_num) override;

  void setRsaKey(const BIGNUM* n, const BIGNUM* e, const BIGNUM* d) {
    n_ = BN_dup(n);
    e_ = BN_dup(e);
    d_ = BN_dup(d);
  };

private:
  uint32_t mbxSetSts(uint32_t status, unsigned req_num, bool success);

  bool supported_instruction_set_;
  BIGNUM* n_{};
  BIGNUM* e_{};
  BIGNUM* d_{};
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
