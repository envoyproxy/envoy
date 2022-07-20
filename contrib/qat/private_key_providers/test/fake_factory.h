#pragma once

#include <openssl/ssl.h>
#include <qat/cpa.h>

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#include "contrib/qat/private_key_providers/source/libqat.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Qat {

class FakeLibQatCryptoImpl : public virtual LibQatCrypto {
public:
  FakeLibQatCryptoImpl() = default;
  CpaStatus icpSalUserStart(std::string) override;
  CpaStatus cpaCyGetNumInstances(Cpa16U* p_num_instances) override;
  CpaStatus cpaCyGetInstances(Cpa16U num_instances, CpaInstanceHandle* cy_instances) override;
  CpaStatus cpaCySetAddressTranslation(const CpaInstanceHandle instance_handle,
                                       CpaVirtualToPhysical virtual2_physical) override;
  CpaStatus cpaCyInstanceGetInfo2(const CpaInstanceHandle instance_handle,
                                  CpaInstanceInfo2* p_instance_info2) override;
  CpaStatus cpaCyStartInstance(CpaInstanceHandle instance_handle) override;
  void* qaeMemAllocNUMA(size_t size, int node, size_t phys_alignment_byte) override;
  void qaeMemFreeNUMA(void** ptr) override;
  CpaStatus cpaCyRsaDecrypt(const CpaInstanceHandle instance_handle,
                            const CpaCyGenFlatBufCbFunc p_rsa_decrypt_cb, void* p_callback_tag,
                            const CpaCyRsaDecryptOpData* p_decrypt_op_data,
                            CpaFlatBuffer* p_output_data) override;
  CpaStatus icpSalUserStop(void) override;
  CpaStatus icpSalCyPollInstance(CpaInstanceHandle instance_handle, Cpa32U response_quota) override;
  CpaStatus cpaCyStopInstance(CpaInstanceHandle instance_handle) override;

  void injectErrors(bool enabled) { inject_errors_ = enabled; }
  void triggerDecrypt() { decrypt_cb_(callback_tag_, CPA_STATUS_SUCCESS, nullptr, output_data_); }
  void* getQatContextPointer() { return callback_tag_; }

  // Set required RSA params out-of-band.
  bool setRsaKey(RSA* rsa);

  CpaStatus icpSalUserStart_return_value_{CPA_STATUS_SUCCESS};
  CpaStatus cpaCyGetNumInstances_return_value_{CPA_STATUS_SUCCESS};
  CpaStatus cpaCyGetInstances_return_value_{CPA_STATUS_SUCCESS};
  CpaStatus cpaCySetAddressTranslation_return_value_{CPA_STATUS_SUCCESS};
  CpaStatus cpaCyInstanceGetInfo2_return_value_{CPA_STATUS_SUCCESS};
  CpaStatus cpaCyStartInstance_return_value_{CPA_STATUS_SUCCESS};
  CpaStatus cpaCyRsaDecrypt_return_value_{CPA_STATUS_SUCCESS};

  void resetReturnValues() {
    icpSalUserStart_return_value_ = CPA_STATUS_SUCCESS;
    cpaCyGetNumInstances_return_value_ = CPA_STATUS_SUCCESS;
    cpaCyGetInstances_return_value_ = CPA_STATUS_SUCCESS;
    cpaCySetAddressTranslation_return_value_ = CPA_STATUS_SUCCESS;
    cpaCyInstanceGetInfo2_return_value_ = CPA_STATUS_SUCCESS;
    cpaCyStartInstance_return_value_ = CPA_STATUS_SUCCESS;
    cpaCyRsaDecrypt_return_value_ = CPA_STATUS_SUCCESS;
  }

private:
  bool inject_errors_{};
  void* callback_tag_{};
  CpaCyGenFlatBufCbFunc decrypt_cb_{};
  CpaFlatBuffer* output_data_{};

  const BIGNUM* n_{};
  const BIGNUM* e_{};
  const BIGNUM* d_{};
};

class FakeQatPrivateKeyMethodFactory : public Ssl::PrivateKeyMethodProviderInstanceFactory {
public:
  FakeQatPrivateKeyMethodFactory() = default;

  // Ssl::PrivateKeyMethodProviderInstanceFactory
  Ssl::PrivateKeyMethodProviderSharedPtr createPrivateKeyMethodProviderInstance(
      const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& message,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) override;
  std::string name() const override { return "qat"; };
};

} // namespace Qat
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
