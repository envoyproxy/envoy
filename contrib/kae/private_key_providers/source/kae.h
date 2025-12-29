#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <vector>

#include "envoy/api/api.h"
#include "envoy/singleton/manager.h"

#include "source/common/common/lock_guard.h"
#include "source/common/common/logger.h"
#include "source/common/common/thread.h"

#include "contrib/kae/private_key_providers/source/libuadk.h"
#include "openssl/err.h"
#include "openssl/rand.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {

static constexpr size_t RSA_BLOCK_SIZE = 4096;
static constexpr size_t RSA_BLOCK_NUM = 16;
static constexpr size_t BIT_BYTES_SHIFT = 3;
static constexpr size_t RSA1024BITS = 1024;
static constexpr size_t RSA2048BITS = 2048;
static constexpr size_t RSA3072BITS = 3072;
static constexpr size_t RSA4096BITS = 4096;

const int KAE_BUFFER_SIZE = 1024;
/**
 * Represents a KAE hardware instance
 */
class KaeHandle : public Logger::Loggable<Logger::Id::connection> {
public:
  KaeHandle() = default;
  ~KaeHandle();

  void setLibUadk(LibUadkCryptoSharedPtr libuadk);
  LibUadkCryptoSharedPtr getLibuadk();
  bool initKaeInstance(LibUadkCryptoSharedPtr libuadk);
  WdHandle getHandle();
  void addUser();
  void removeUser();
  bool hasUsers();
  int getNodeAffinity();
  bool isDone();
  void* getRsaCtx();
  wcrypto_rsa_ctx_setup& getRsaCtxSetup();
  void* getMemPool();

  Thread::ThreadPtr polling_thread_;
  Thread::MutexBasicLockable poll_lock_{};
  Thread::CondVar kae_thread_cond_{};

private:
  WdHandle handle_;
  void* rsa_ctx_;
  void* mem_pool_{};
  wcrypto_rsa_ctx_setup rsa_setup_{};
  LibUadkCryptoSharedPtr libuadk_;
  int users_{};
  bool done_{};
};

/**
 * KaeSection represents a section definition in KAE configuration. Its main purpose is to
 * initialize HW and load balance operations to the KAE handles.
 */

class KaeSection : public Logger::Loggable<Logger::Id::connection> {
public:
  KaeSection(LibUadkCryptoSharedPtr libuadk);
  bool startSection(Api::Api& api, std::chrono::milliseconds poll_delay);
  KaeHandle& getNextHandle();
  bool isInitialized();

private:
  Thread::MutexBasicLockable handle_lock_{};
  uint32_t num_instances_{};
  std::vector<KaeHandle> kae_handles_;
  int next_handle_{};
  LibUadkCryptoSharedPtr libuadk_;
};

/**
 * KaeManager is a singleton to oversee KAE hardware.
 */
class KaeManager : public std::enable_shared_from_this<KaeManager>,
                   public Singleton::Instance,
                   public Logger::Loggable<Logger::Id::connection> {
public:
  static void kaePoll(KaeHandle& handle, std::chrono::milliseconds poll_delay);

  KaeManager(LibUadkCryptoSharedPtr libuadk);
  ~KaeManager() override;

  static int connectionIndex();
  static int contextIndex();

  bool checkKaeDevice();

private:
  LibUadkCryptoSharedPtr libuadk_;
  bool kae_is_supported_{true};
};

/**
 * Represents a single KAE operation context.
 */
class KaeContext {
public:
  KaeContext(KaeHandle& handle);
  ~KaeContext();
  bool init();
  KaeHandle& getHandle();
  int getDecryptedDataLength();
  unsigned char* getDecryptedData();
  bool copyDecryptedData(unsigned char* bytes, int len);
  void setOpStatus(int status);
  int getOpStatus();
  int getFd();
  int getWriteFd();
  bool decrypt(int len, const unsigned char* from, RSA* rsa, int padding);
  void* allocBlk();
  void freeBlk(void* blk);
  void freeDecryptOpBuf(wcrypto_rsa_op_data* opdata);
  void* getRsaCtx();
  wcrypto_rsa_op_data* getOpData();

  Thread::MutexBasicLockable data_lock_{};

  LibUadkCryptoSharedPtr getLibuadk();

private:
  bool checkBitUseful(const int bit);
  bool convertBnToFlatbuffer(wd_dtb* fb, const BIGNUM* bn);
  bool buildRsaOpBuf(int flen, const unsigned char* from, RSA* rsa, int padding,
                     wcrypto_rsa_op_data** op_data);
  KaeHandle& handle_;
  wcrypto_rsa_op_data* op_data_{};
  int last_status_{WD_STATUS_BUSY};
  std::array<unsigned char, KAE_BUFFER_SIZE> decrypted_data_;
  int decrypted_data_length_{0};
  // Pipe for passing the message that the operation is completed.
  int read_fd_{-1};
  int write_fd_{-1};
};

} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
