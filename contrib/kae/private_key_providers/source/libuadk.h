#pragma once

#include <uadk/v1/wd_bmm.h>

#include <memory>

#include "envoy/common/pure.h"

#include "uadk/v1/wd.h"
#include "uadk/v1/wd_rsa.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {

#define WD_STATUS_BUSY (-WD_EBUSY)
#define WD_STATUS_FAILED (-1)

using WdHandle = wd_queue*;

class LibUadkCrypto {
public:
  virtual ~LibUadkCrypto() = default;
  virtual int kaeGetNumInstances(uint32_t* p_num_instances) PURE;
  virtual void kaeStopInstance(WdHandle handle) PURE;
  virtual int kaeRequestQueue(WdHandle handle) PURE;
  virtual int kaeDoRsa(void* ctx, wcrypto_rsa_op_data* opdata, void* tag) PURE;
  virtual int kaeRsaPoll(WdHandle handle, unsigned int num) PURE;
  virtual void* kaeBlkPoolCreate(WdHandle handle, wd_blkpool_setup* setup) PURE;
  virtual void kaeBlkPoolDestory(void* pool) PURE;
  virtual void kaeGetRsaCrtPrikeyParams(wcrypto_rsa_prikey* pvk, wd_dtb** dq, wd_dtb** dp,
                                        wd_dtb** qinv, wd_dtb** q, wd_dtb** p) PURE;
  virtual void kaeGetRsaPrikey(void* ctx, wcrypto_rsa_prikey** prikey) PURE;

  virtual void* kaeCreateRsaCtx(wd_queue* q, wcrypto_rsa_ctx_setup* setup) PURE;
  virtual void kaeDelRsaCtx(void* ctx) PURE;
  virtual int kaeGetAvailableDevNum(const char* algorithm) PURE;
};

using LibUadkCryptoSharedPtr = std::shared_ptr<LibUadkCrypto>;

} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
