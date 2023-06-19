#include "fake_factory.h"

#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/datasource.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"

#include "contrib/envoy/extensions/private_key_providers/qat/v3alpha/qat.pb.h"
#include "contrib/envoy/extensions/private_key_providers/qat/v3alpha/qat.pb.validate.h"
#include "contrib/qat/private_key_providers/source/qat_private_key_provider.h"
#include "openssl/rsa.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Qat {

CpaStatus FakeLibQatCryptoImpl::FakeLibQatCryptoImpl::icpSalUserStart(std::string name) {
  UNREFERENCED_PARAMETER(name);
  return icpSalUserStart_return_value_;
}

CpaStatus FakeLibQatCryptoImpl::cpaCyGetNumInstances(Cpa16U* p_num_instances) {
  *p_num_instances = 1;
  return cpaCyGetNumInstances_return_value_;
}

CpaStatus FakeLibQatCryptoImpl::cpaCyGetInstances(Cpa16U num_instances,
                                                  CpaInstanceHandle* cy_instances) {
  UNREFERENCED_PARAMETER(num_instances);
  UNREFERENCED_PARAMETER(cy_instances);
  if (num_instances == 1) {
    cy_instances[0] = CPA_INSTANCE_HANDLE_SINGLE;
  }
  return cpaCyGetInstances_return_value_;
}

CpaStatus FakeLibQatCryptoImpl::cpaCySetAddressTranslation(const CpaInstanceHandle instance_handle,
                                                           CpaVirtualToPhysical virtual2_physical) {
  UNREFERENCED_PARAMETER(instance_handle);
  UNREFERENCED_PARAMETER(virtual2_physical);
  return cpaCySetAddressTranslation_return_value_;
}

CpaStatus FakeLibQatCryptoImpl::cpaCyInstanceGetInfo2(const CpaInstanceHandle instance_handle,
                                                      CpaInstanceInfo2* p_instance_info2) {
  UNREFERENCED_PARAMETER(instance_handle);
  UNREFERENCED_PARAMETER(p_instance_info2);
  return cpaCyInstanceGetInfo2_return_value_;
}

CpaStatus FakeLibQatCryptoImpl::cpaCyStartInstance(CpaInstanceHandle instance_handle) {
  UNREFERENCED_PARAMETER(instance_handle);
  return cpaCyStartInstance_return_value_;
}

void* FakeLibQatCryptoImpl::qaeMemAllocNUMA(size_t size, int node, size_t phys_alignment_byte) {
  UNREFERENCED_PARAMETER(node);
  UNREFERENCED_PARAMETER(phys_alignment_byte);
  return malloc(size);
}

void FakeLibQatCryptoImpl::qaeMemFreeNUMA(void** ptr) {
  if (ptr) {
    free(*ptr);
  }
};

bool FakeLibQatCryptoImpl::setRsaKey(RSA* rsa) {
  ASSERT(rsa != nullptr);

  RSA_get0_key(rsa, &n_, &e_, &d_);

  if (n_ == nullptr || e_ == nullptr || d_ == nullptr) {
    return false;
  }

  return true;
};

CpaStatus FakeLibQatCryptoImpl::cpaCyRsaDecrypt(const CpaInstanceHandle instance_handle,
                                                const CpaCyGenFlatBufCbFunc p_rsa_decrypt_cb,
                                                void* p_callback_tag,
                                                const CpaCyRsaDecryptOpData* p_decrypt_op_data,
                                                CpaFlatBuffer* p_output_data) {
  UNREFERENCED_PARAMETER(instance_handle);

  output_data_ = p_output_data;
  callback_tag_ = p_callback_tag;

  decrypt_cb_ = p_rsa_decrypt_cb;

  CpaCyRsaPrivateKey* key = p_decrypt_op_data->pRecipientPrivateKey;

  // Re-create the RSA object from the key data.
  BIGNUM* p = BN_bin2bn(key->privateKeyRep2.prime1P.pData,
                        key->privateKeyRep2.prime1P.dataLenInBytes, nullptr);
  BIGNUM* q = BN_bin2bn(key->privateKeyRep2.prime2Q.pData,
                        key->privateKeyRep2.prime2Q.dataLenInBytes, nullptr);
  BIGNUM* dmp1 = BN_bin2bn(key->privateKeyRep2.exponent1Dp.pData,
                           key->privateKeyRep2.exponent1Dp.dataLenInBytes, nullptr);
  BIGNUM* dmq1 = BN_bin2bn(key->privateKeyRep2.exponent2Dq.pData,
                           key->privateKeyRep2.exponent2Dq.dataLenInBytes, nullptr);
  BIGNUM* iqmp = BN_bin2bn(key->privateKeyRep2.coefficientQInv.pData,
                           key->privateKeyRep2.coefficientQInv.dataLenInBytes, nullptr);

  RSA* rsa = RSA_new();

  RSA_set0_factors(rsa, p, q);
  RSA_set0_crt_params(rsa, dmp1, dmq1, iqmp);

  if (n_ == nullptr || e_ == nullptr || d_ == nullptr) {
    ASSERT(false);
  }

  // BoringSSL needs these factors. They are set out-of-band.
  RSA_set0_key(rsa, BN_dup(n_), BN_dup(e_), BN_dup(d_));

  // Run the decrypt operation.
  output_data_->pData = static_cast<Cpa8U*>(qaeMemAllocNUMA(RSA_size(rsa), 0, 0));
  int ret = RSA_private_decrypt(RSA_size(rsa), p_decrypt_op_data->inputData.pData,
                                output_data_->pData, rsa, RSA_NO_PADDING);
  if (ret < 0) {
    return CPA_STATUS_FAIL;
  }

  output_data_->dataLenInBytes = ret;

  RSA_free(rsa);

  return cpaCyRsaDecrypt_return_value_;
};

CpaStatus FakeLibQatCryptoImpl::icpSalUserStop(void) { return CPA_STATUS_SUCCESS; };

CpaStatus FakeLibQatCryptoImpl::icpSalCyPollInstance(CpaInstanceHandle instance_handle,
                                                     Cpa32U response_quota) {
  UNREFERENCED_PARAMETER(instance_handle);
  UNREFERENCED_PARAMETER(response_quota);
  return CPA_STATUS_SUCCESS;
};

CpaStatus FakeLibQatCryptoImpl::cpaCyStopInstance(CpaInstanceHandle instance_handle) {
  UNREFERENCED_PARAMETER(instance_handle);
  return CPA_STATUS_SUCCESS;
}

Ssl::PrivateKeyMethodProviderSharedPtr
FakeQatPrivateKeyMethodFactory::createPrivateKeyMethodProviderInstance(
    const envoy::extensions::transport_sockets::tls::v3::PrivateKeyProvider& proto_config,
    Server::Configuration::TransportSocketFactoryContext& private_key_provider_context) {
  ProtobufTypes::MessagePtr message = std::make_unique<
      envoy::extensions::private_key_providers::qat::v3alpha::QatPrivateKeyMethodConfig>();

  Config::Utility::translateOpaqueConfig(proto_config.typed_config(),
                                         ProtobufMessage::getNullValidationVisitor(), *message);
  const envoy::extensions::private_key_providers::qat::v3alpha::QatPrivateKeyMethodConfig conf =
      MessageUtil::downcastAndValidate<
          const envoy::extensions::private_key_providers::qat::v3alpha::QatPrivateKeyMethodConfig&>(
          *message, private_key_provider_context.messageValidationVisitor());

  std::shared_ptr<FakeLibQatCryptoImpl> libqat = std::make_shared<FakeLibQatCryptoImpl>();
  return std::make_shared<QatPrivateKeyMethodProvider>(conf, private_key_provider_context, libqat);
}

} // namespace Qat
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
