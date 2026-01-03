#include "fake_factory.h"

#include <cstdint>
#include <cstdlib>
#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/private_key_providers/kae/v3alpha/kae.pb.h"
#include "contrib/envoy/extensions/private_key_providers/kae/v3alpha/kae.pb.validate.h"
#include "contrib/kae/private_key_providers/source/kae_private_key_provider.h"
#include "openssl/rsa.h"
#include "openssl/ssl.h"

struct wcrypto_rsa_prikey { // NOLINT(readability-identifier-naming)
  struct wd_dtb p;
  struct wd_dtb q;
  struct wd_dtb dp;
  struct wd_dtb dq;
  struct wd_dtb qinv;
  uint32_t key_size;
  void* data[];
};

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {

FakeLibUadkCryptoImpl::FakeLibUadkCryptoImpl() {
  msg_ = static_cast<wcrypto_rsa_msg*>(malloc(sizeof(wcrypto_rsa_msg)));
  msg_->out = static_cast<uint8_t*>(malloc(4096));
}

FakeLibUadkCryptoImpl::~FakeLibUadkCryptoImpl() {
  if (msg_ && msg_->out) {
    free(msg_->out);
  }

  if (msg_) {
    free(msg_);
    msg_ = nullptr;
  }

  if (prikey_) {
    delete[] prikey_->p.data;
    delete[] prikey_->q.data;
    delete[] prikey_->dp.data;
    delete[] prikey_->dq.data;
    delete[] prikey_->qinv.data;
    delete[] prikey_;
  }
}

int FakeLibUadkCryptoImpl::kaeGetAvailableDevNum(const char* algorithm) {
  UNREFERENCED_PARAMETER(algorithm);
  return 1;
}

int FakeLibUadkCryptoImpl::kaeGetNumInstances(uint32_t* p_num_instances) {
  *p_num_instances = 1;
  return kaeGetNumInstances_return_value_;
}

void FakeLibUadkCryptoImpl::kaeStopInstance(WdHandle handle) { UNREFERENCED_PARAMETER(handle); }

int FakeLibUadkCryptoImpl::kaeRsaPoll(WdHandle handle, unsigned int num) {
  UNREFERENCED_PARAMETER(handle);
  UNREFERENCED_PARAMETER(num);
  return WD_SUCCESS;
}

int FakeLibUadkCryptoImpl::kaeRequestQueue(WdHandle handle) {
  UNREFERENCED_PARAMETER(handle);
  return WD_SUCCESS;
}

void* FakeLibUadkCryptoImpl::kaeBlkPoolCreate(WdHandle handle, wd_blkpool_setup* setup) {
  UNREFERENCED_PARAMETER(handle);
  UNREFERENCED_PARAMETER(setup);
  mem_pool_ = malloc(sizeof(void*));
  return mem_pool_;
}

void FakeLibUadkCryptoImpl::kaeBlkPoolDestory(void* pool) {
  if (pool) {
    free(pool);
  }
}

void FakeLibUadkCryptoImpl::kaeGetRsaPrikey(void* ctx, wcrypto_rsa_prikey** prikey) {
  UNREFERENCED_PARAMETER(ctx);
  prikey_ = new wcrypto_rsa_prikey;
  *prikey = prikey_;
}

namespace {

void* hookMalloc(void* pool, size_t size) {
  UNREFERENCED_PARAMETER(pool);
  UNREFERENCED_PARAMETER(size);
  return malloc(4096);
}

void hookFree(void* pool, void* blk) {
  UNREFERENCED_PARAMETER(pool);
  free(blk);
}

} // namespace

void FakeLibUadkCryptoImpl::kaeGetRsaCrtPrikeyParams(wcrypto_rsa_prikey* pvk, wd_dtb** dq,
                                                     wd_dtb** dp, wd_dtb** qinv, wd_dtb** q,
                                                     wd_dtb** p) {
  pvk->key_size = 4096;
  int key_size = pvk->key_size;
  pvk->p.data = new char[key_size];
  pvk->q.data = new char[key_size];
  pvk->dp.data = new char[key_size];
  pvk->dq.data = new char[key_size];
  pvk->qinv.data = new char[key_size];
  memset(pvk->p.data, 0, key_size);
  memset(pvk->q.data, 0, key_size);
  memset(pvk->dp.data, 0, key_size);
  memset(pvk->dq.data, 0, key_size);
  memset(pvk->qinv.data, 0, key_size);
  *dp = &pvk->dp;
  *dq = &pvk->dq;
  *p = &pvk->p;
  *q = &pvk->q;
  *qinv = &pvk->qinv;
}

void* FakeLibUadkCryptoImpl::kaeCreateRsaCtx(wd_queue* q, wcrypto_rsa_ctx_setup* setup) {
  UNREFERENCED_PARAMETER(q);
  rsa_setup_ = setup;

  rsa_setup_->br.alloc = hookMalloc;
  rsa_setup_->br.free = hookFree;
  rsa_ctx_ = malloc(sizeof(int));
  return rsa_ctx_;
}

void FakeLibUadkCryptoImpl::kaeDelRsaCtx(void* ctx) { free(ctx); }

bool FakeLibUadkCryptoImpl::setRsaKey(RSA* rsa) {
  ASSERT(rsa != nullptr);

  RSA_get0_key(rsa, &n_, &e_, &d_);

  if (n_ == nullptr || e_ == nullptr || d_ == nullptr) {
    return false;
  }

  return true;
}

int FakeLibUadkCryptoImpl::kaeDoRsa(void* ctx, wcrypto_rsa_op_data* opdata, void* tag) {
  UNREFERENCED_PARAMETER(ctx);
  output_data_ = static_cast<unsigned char*>(opdata->out);
  callback_tag_ = tag;

  decrypt_cb_ = rsa_setup_->cb;

  BIGNUM* p =
      BN_bin2bn(reinterpret_cast<unsigned char*>(prikey_->p.data), prikey_->p.dsize, nullptr);
  BIGNUM* q =
      BN_bin2bn(reinterpret_cast<unsigned char*>(prikey_->q.data), prikey_->q.dsize, nullptr);
  BIGNUM* dmp1 =
      BN_bin2bn(reinterpret_cast<unsigned char*>(prikey_->dp.data), prikey_->dp.dsize, nullptr);
  BIGNUM* dmq1 =
      BN_bin2bn(reinterpret_cast<unsigned char*>(prikey_->dq.data), prikey_->dq.dsize, nullptr);
  BIGNUM* iqmp =
      BN_bin2bn(reinterpret_cast<unsigned char*>(prikey_->qinv.data), prikey_->qinv.dsize, nullptr);

  RSA* rsa = RSA_new();

  RSA_set0_factors(rsa, p, q);
  RSA_set0_crt_params(rsa, dmp1, dmq1, iqmp);

  if (n_ == nullptr || e_ == nullptr || d_ == nullptr) {
    ASSERT(false);
  }

  // BoringSSL needs these factors. They are set out-of-band.
  RSA_set0_key(rsa, BN_dup(n_), BN_dup(e_), BN_dup(d_));

  // Run the decrypt operation.
  int ret = RSA_private_decrypt(RSA_size(rsa), static_cast<uint8_t*>(opdata->in), output_data_, rsa,
                                RSA_NO_PADDING);
  if (ret < 0) {
    RSA_free(rsa);
    return -1;
  }

  output_data_len_ = ret;

  msg_->result = WD_SUCCESS;
  memcpy(msg_->out, output_data_, output_data_len_);
  msg_->out_bytes = output_data_len_;

  RSA_free(rsa);

  return kaeDoRSA_return_value_;
}

Ssl::PrivateKeyMethodProviderSharedPtr
FakeKaePrivateKeyMethodFactory::createPrivateKeyMethodProviderInstance(
    const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& proto_config,
    Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) {
  ProtobufTypes::MessagePtr message = std::make_unique<
      envoy::extensions::private_key_providers::kae::v3alpha::KaePrivateKeyMethodConfig>();

  THROW_IF_NOT_OK(Config::Utility::translateOpaqueConfig(
      proto_config.typed_config(), ProtobufMessage::getNullValidationVisitor(), *message));
  const envoy::extensions::private_key_providers::kae::v3alpha::KaePrivateKeyMethodConfig conf =
      MessageUtil::downcastAndValidate<
          const envoy::extensions::private_key_providers::kae::v3alpha::KaePrivateKeyMethodConfig&>(
          *message, private_key_provider_context.messageValidationVisitor());

  std::shared_ptr<FakeLibUadkCryptoImpl> libuadk = std::make_shared<FakeLibUadkCryptoImpl>();
  return std::make_shared<KaePrivateKeyMethodProvider>(conf, private_key_provider_context, libuadk);
}

} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
