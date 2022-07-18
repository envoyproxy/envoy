#pragma once

#include "contrib/qat/private_key_providers/source/libqat.h"
#include "qat/cpa_cy_im.h"
#include "qat/icp_sal_poll.h"
#include "qat/icp_sal_user.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Qat {

class LibQatCryptoImpl : public virtual LibQatCrypto {
public:
  CpaStatus icpSalUserStart(const std::string name) override {
    return ::icp_sal_userStart(name.c_str());
  }

  CpaStatus cpaCyGetNumInstances(Cpa16U* p_num_instances) override {
    return ::cpaCyGetNumInstances(p_num_instances);
  }

  CpaStatus cpaCyGetInstances(Cpa16U num_instances, CpaInstanceHandle* cy_instances) override {
    return ::cpaCyGetInstances(num_instances, cy_instances);
  }

  CpaStatus cpaCySetAddressTranslation(const CpaInstanceHandle instance_handle,
                                       CpaVirtualToPhysical virtual2_physical) override {
    return ::cpaCySetAddressTranslation(instance_handle, virtual2_physical);
  }

  CpaStatus cpaCyInstanceGetInfo2(const CpaInstanceHandle instance_handle,
                                  CpaInstanceInfo2* p_instance_info2) override {
    return ::cpaCyInstanceGetInfo2(instance_handle, p_instance_info2);
  }

  CpaStatus cpaCyStartInstance(CpaInstanceHandle instance_handle) override {
    return ::cpaCyStartInstance(instance_handle);
  }

  void* qaeMemAllocNUMA(size_t size, int node, size_t phys_alignment_byte) override {
    return ::qaeMemAllocNUMA(size, node, phys_alignment_byte);
  }

  void qaeMemFreeNUMA(void** ptr) override { return ::qaeMemFreeNUMA(ptr); };

  CpaStatus cpaCyRsaDecrypt(const CpaInstanceHandle instance_handle,
                            const CpaCyGenFlatBufCbFunc p_rsa_decrypt_cb, void* p_callback_tag,
                            const CpaCyRsaDecryptOpData* p_decrypt_op_data,
                            CpaFlatBuffer* p_output_data) override {
    return ::cpaCyRsaDecrypt(instance_handle, p_rsa_decrypt_cb, p_callback_tag, p_decrypt_op_data,
                             p_output_data);
  };

  CpaStatus icpSalUserStop(void) override { return ::icp_sal_userStop(); };

  CpaStatus icpSalCyPollInstance(CpaInstanceHandle instance_handle,
                                 Cpa32U response_quota) override {
    return ::icp_sal_CyPollInstance(instance_handle, response_quota);
  }

  CpaStatus cpaCyStopInstance(CpaInstanceHandle instance_handle) override {
    return ::cpaCyStopInstance(instance_handle);
  };
};

} // namespace Qat
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
