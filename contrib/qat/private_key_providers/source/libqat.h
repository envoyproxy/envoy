#pragma once

#include "envoy/common/pure.h"

#include "openssl/ssl.h"
#include "qat/cpa.h"
#include "qat/cpa_cy_rsa.h"
#include "qat/qae_mem.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Qat {

class LibQatCrypto {
public:
  virtual ~LibQatCrypto() = default;

  virtual CpaStatus icpSalUserStart(const std::string name) PURE;
  virtual CpaStatus cpaCyGetNumInstances(Cpa16U* p_num_instances) PURE;
  virtual CpaStatus cpaCyGetInstances(Cpa16U num_instances, CpaInstanceHandle* cy_instances) PURE;
  virtual CpaStatus cpaCySetAddressTranslation(const CpaInstanceHandle instance_handle,
                                               CpaVirtualToPhysical virtual2_physical) PURE;
  virtual CpaStatus cpaCyInstanceGetInfo2(const CpaInstanceHandle instance_handle,
                                          CpaInstanceInfo2* p_instance_info2) PURE;
  virtual CpaStatus cpaCyStartInstance(CpaInstanceHandle instance_handle) PURE;
  virtual void* qaeMemAllocNUMA(size_t size, int node, size_t phys_alignment_byte) PURE;
  virtual void qaeMemFreeNUMA(void** ptr) PURE;
  virtual CpaStatus cpaCyRsaDecrypt(const CpaInstanceHandle instance_handle,
                                    const CpaCyGenFlatBufCbFunc p_rsa_decrypt_cb,
                                    void* p_callback_tag,
                                    const CpaCyRsaDecryptOpData* p_decrypt_op_data,
                                    CpaFlatBuffer* p_output_data) PURE;
  virtual CpaStatus icpSalUserStop(void) PURE;
  virtual CpaStatus icpSalCyPollInstance(CpaInstanceHandle instance_handle,
                                         Cpa32U response_quota) PURE;
  virtual CpaStatus cpaCyStopInstance(CpaInstanceHandle instance_handle) PURE;
};

using LibQatCryptoSharedPtr = std::shared_ptr<LibQatCrypto>;

} // namespace Qat
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
