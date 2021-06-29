#pragma once

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#include "source/extensions/private_key_providers/cryptomb/cryptomb_private_key_provider.h"
#include "source/extensions/private_key_providers/cryptomb/ipp.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

class FakeIppCryptoImpl : public virtual IppCrypto {
public:
  FakeIppCryptoImpl(bool supported_instruction_set);
  ~FakeIppCryptoImpl() override;

  int mbxIsCryptoMbApplicable(int64u features) override;
  mbx_status mbxNistp256EcdsaSignSslMb8(int8u* pa_sign_r[8], int8u* pa_sign_s[8],
                                        const int8u* const pa_msg[8],
                                        const BIGNUM* const pa_eph_skey[8],
                                        const BIGNUM* const pa_reg_skey[8],
                                        int8u* p_buffer) override;
  mbx_status mbxRsaPrivateCrtSslMb8(const int8u* const from_pa[8], int8u* const to_pa[8],
                                    const BIGNUM* const p_pa[8], const BIGNUM* const q_pa[8],
                                    const BIGNUM* const dp_pa[8], const BIGNUM* const dq_pa[8],
                                    const BIGNUM* const iq_pa[8],
                                    int expected_rsa_bitsize) override;
  mbx_status mbxRsaPublicSslMb8(const int8u* const from_pa[8], int8u* const to_pa[8],
                                const BIGNUM* const e_pa[8], const BIGNUM* const n_pa[8],
                                int expected_rsa_bitsize) override;

  void setRsaKey(const BIGNUM* n, const BIGNUM* e, const BIGNUM* d) {
    n_ = BN_dup(n);
    e_ = BN_dup(e);
    d_ = BN_dup(d);
  };

private:
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
