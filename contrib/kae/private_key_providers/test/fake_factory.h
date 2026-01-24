#pragma once

#include <openssl/ssl.h>

#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/ssl/private_key/private_key.h"
#include "envoy/ssl/private_key/private_key_config.h"

#include "contrib/kae/private_key_providers/source/libuadk.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {

class FakeLibUadkCryptoImpl : public virtual LibUadkCrypto {
public:
  FakeLibUadkCryptoImpl();
  ~FakeLibUadkCryptoImpl() override;
  int kaeGetNumInstances(uint32_t* p_num_instances) override;
  void kaeStopInstance(WdHandle handle) override;
  int kaeDoRsa(void* ctx, wcrypto_rsa_op_data* opdata, void* tag) override;
  int kaeRequestQueue(WdHandle handle) override;
  int kaeRsaPoll(WdHandle handle, unsigned int num) override;
  void* kaeBlkPoolCreate(WdHandle handle, wd_blkpool_setup* setup) override;
  void kaeBlkPoolDestory(void* pool) override;
  void kaeGetRsaCrtPrikeyParams(wcrypto_rsa_prikey* pvk, wd_dtb** dq, wd_dtb** dp, wd_dtb** qinv,
                                wd_dtb** q, wd_dtb** p) override;
  void kaeGetRsaPrikey(void* ctx, wcrypto_rsa_prikey** prikey) override;

  void* kaeCreateRsaCtx(wd_queue* q, wcrypto_rsa_ctx_setup* setup) override;
  void kaeDelRsaCtx(void* ctx) override;
  int kaeGetAvailableDevNum(const char* algorithm) override;

  void injectErrors(bool enabled) { inject_errors_ = enabled; }
  void triggerDecrypt() { decrypt_cb_(msg_, callback_tag_); }
  void* getKaeContextPointer() { return callback_tag_; }

  bool setRsaKey(RSA* rsa);

  int kaeDoRSA_return_value_{WD_SUCCESS};
  int kaeRsaPoll_return_value_{WD_SUCCESS};
  int kaeGetNumInstances_return_value_{WD_SUCCESS};

  void resetReturnValues() {
    kaeDoRSA_return_value_ = WD_SUCCESS;
    kaeRsaPoll_return_value_ = WD_SUCCESS;
    kaeGetNumInstances_return_value_ = WD_SUCCESS;
  }

private:
  bool inject_errors_{};
  void* callback_tag_{};
  void* mem_pool_;
  wcrypto_rsa_msg* msg_{};
  wcrypto_cb decrypt_cb_{};
  wcrypto_rsa_prikey* prikey_{};
  wcrypto_rsa_ctx_setup* rsa_setup_{};
  void* rsa_ctx_{};
  unsigned char* output_data_{};
  int output_data_len_{};

  const BIGNUM* n_{};
  const BIGNUM* e_{};
  const BIGNUM* d_{};
};

class FakeKaePrivateKeyMethodFactory : public Ssl::PrivateKeyMethodProviderInstanceFactory {
public:
  FakeKaePrivateKeyMethodFactory() = default;

  // Ssl::PrivateKeyMethodProviderInstanceFactory
  Ssl::PrivateKeyMethodProviderSharedPtr createPrivateKeyMethodProviderInstance(
      const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& message,
      Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) override;
  std::string name() const override { return "kae"; };
};

} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
