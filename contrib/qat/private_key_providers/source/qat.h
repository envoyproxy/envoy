#pragma once

#include <map>

#include "envoy/api/api.h"
#include "envoy/singleton/manager.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"

#include "contrib/qat/private_key_providers/source/libqat.h"
#include "openssl/err.h"
#include "openssl/rand.h"
#include "openssl/x509v3.h"

// qatlib
#include "qat/cpa.h"
#include "qat/cpa_cy_im.h"
#include "qat/cpa_cy_rsa.h"
#include "qat/icp_sal_poll.h"
#include "qat/icp_sal_user.h"
#include "qat/qae_mem.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Qat {

const int QAT_BUFFER_SIZE = 1024;

/**
 * Represents a QAT hardware instance.
 */
class QatHandle : public Logger::Loggable<Logger::Id::connection> {
public:
  QatHandle() = default;
  ~QatHandle();
  // TODO(ipuustin): getters and setters
  void setLibqat(LibQatCryptoSharedPtr libqat);
  LibQatCryptoSharedPtr getLibqat();
  bool initQatInstance(CpaInstanceHandle handle, LibQatCryptoSharedPtr libqat);
  CpaInstanceHandle getHandle();
  void addUser();
  void removeUser();
  bool hasUsers();
  int getNodeAffinity();
  int isDone();

  Thread::ThreadPtr polling_thread_{};
  Thread::MutexBasicLockable poll_lock_{};
  Thread::CondVar qat_thread_cond_{};

private:
  CpaInstanceHandle handle_;
  CpaInstanceInfo2 info_;
  LibQatCryptoSharedPtr libqat_{};
  int users_{};
  bool done_{};
};

/**
 * QatSection represents a section definition in QAT configuration. Its main purpose is to initalize
 * HW and load balance operations to the QAT handles.
 */
class QatSection : public Logger::Loggable<Logger::Id::connection> {
public:
  QatSection(LibQatCryptoSharedPtr libqat);
  bool startSection(Api::Api& api, std::chrono::milliseconds poll_delay);
  QatHandle& getNextHandle();
  bool isInitialized();

private:
  Thread::MutexBasicLockable handle_lock_{};
  Cpa16U num_instances_{};
  std::vector<QatHandle> qat_handles_;
  int next_handle_{};
  LibQatCryptoSharedPtr libqat_{};
};

/**
 * QatManager is a singleton to oversee QAT hardware.
 */
class QatManager : public std::enable_shared_from_this<QatManager>,
                   public Singleton::Instance,
                   public Logger::Loggable<Logger::Id::connection> {
public:
  static void qatPoll(QatHandle& handle, std::chrono::milliseconds poll_delay);

  QatManager(LibQatCryptoSharedPtr libqat);
  ~QatManager() override;

  static int connectionIndex();
  static int contextIndex();

private:
  LibQatCryptoSharedPtr libqat_{};
};

/**
 * Represents a single QAT operation context.
 */
class QatContext {
public:
  QatContext(QatHandle& handle);
  ~QatContext();
  bool init();
  QatHandle& getHandle();
  int getDecryptedDataLength();
  unsigned char* getDecryptedData();
  bool copyDecryptedData(unsigned char* bytes, int len);
  void setOpStatus(CpaStatus status);
  CpaStatus getOpStatus();
  int getFd();
  int getWriteFd();
  bool decrypt(int len, const unsigned char* from, RSA* rsa, int padding);
  void freeDecryptOpBuf(CpaCyRsaDecryptOpData* dec_op_data, CpaFlatBuffer* out_buf);
  void freeNuma(void* ptr);

  Thread::MutexBasicLockable data_lock_{};

  LibQatCryptoSharedPtr getLibqat();

private:
  // TODO(ipuustin): QatHandle might be a more logical place for these.
  bool convertBnToFlatbuffer(CpaFlatBuffer* fb, const BIGNUM* bn);
  bool buildDecryptOpBuf(int flen, const unsigned char* from, RSA* rsa, int padding,
                         CpaCyRsaDecryptOpData** dec_op_data, CpaFlatBuffer** output_buffer);

  QatHandle& handle_;
  CpaStatus last_status_;
  unsigned char decrypted_data_[QAT_BUFFER_SIZE];
  int decrypted_data_length_;
  // Pipe for passing the message that the operation is completed.
  int read_fd_;
  int write_fd_;
};

} // namespace Qat
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
