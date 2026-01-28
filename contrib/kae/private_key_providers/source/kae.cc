#include "contrib/kae/private_key_providers/source/kae.h"
#include "kae.h"

#include <openssl/bn.h>
#include <openssl/crypto.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <thread>

#include "libuadk.h"
#include "libuadk_impl.h"
#include "openssl/rsa.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {

// KAE Manager
KaeManager::KaeManager(LibUadkCryptoSharedPtr libuadk) {

  libuadk_ = libuadk;
  int kae_dev_num = libuadk_->kaeGetAvailableDevNum(RSA_ALG);
  if (kae_dev_num == 0) {
    ENVOY_LOG(warn, "Failed to start KAE device.");
    kae_is_supported_ = false;
  }
}

KaeManager::~KaeManager() = default;

void KaeManager::kaePoll(KaeHandle& handle, std::chrono::milliseconds poll_delay) {
  ENVOY_LOG(debug, "create KAE polling thread");
  while (1) {
    {
      Thread::LockGuard poll_lock(handle.poll_lock_);
      if (handle.isDone()) {
        return;
      }

      if (!handle.hasUsers()) {
        handle.kae_thread_cond_.wait(handle.poll_lock_);
      }
    }
    handle.getLibuadk()->kaeRsaPoll(handle.getHandle(), 0);

    std::this_thread::sleep_for(poll_delay); // NO_CHECK_FORMAT(real_time)
  }
  ENVOY_LOG(debug, "join kae polling thread");
}

namespace {
int createIndex() {
  int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  RELEASE_ASSERT(index >= 0, "Failed to get SSL user data index");
  return index;
}
} // namespace

int KaeManager::connectionIndex() { CONSTRUCT_ON_FIRST_USE(int, createIndex()); }

int KaeManager::contextIndex() { CONSTRUCT_ON_FIRST_USE(int, createIndex()); }

bool KaeManager::checkKaeDevice() { return kae_is_supported_; }

// KAE handle

namespace {

void* kaeWdAllocBlk(void* pool, size_t size) {
  UNREFERENCED_PARAMETER(size);
  return wd_alloc_blk(pool);
}

void kaeWdFreeBlk(void* pool, void* blk) { wd_free_blk(pool, blk); }

void* kaeDmaMap(void* user, void* va, size_t size) {
  UNREFERENCED_PARAMETER(size);
  return wd_blk_iova_map(user, va);
}

void kaeDmaUnmap(void* user, void* va, void* dma, size_t size) {
  UNREFERENCED_PARAMETER(size);
  wd_blk_iova_unmap(user, dma, va);
}
} // namespace

namespace {
// message is wcrypto_rsa_msg. tag is user data, kaeContext
static void rsaCb(const void* message, void* tag) {

  KaeContext* ctx = static_cast<KaeContext*>(tag);
  ASSERT(ctx != nullptr && message != nullptr);

  const wcrypto_rsa_msg* msg = static_cast<const wcrypto_rsa_msg*>(message);
  int status = msg->result;

  KaeHandle& handle = ctx->getHandle();
  {
    Thread::LockGuard poll_lock(handle.poll_lock_);
    handle.removeUser();
  }

  {
    Thread::LockGuard data_lock(ctx->data_lock_);
    if (!ctx->copyDecryptedData(static_cast<unsigned char*>(msg->out),
                                static_cast<int>(msg->out_bytes))) {
      status = WD_STATUS_FAILED;
    }

    ctx->freeDecryptOpBuf(ctx->getOpData());

    int ret = write(ctx->getWriteFd(), &status, sizeof(status));
    UNREFERENCED_PARAMETER(ret);
  }
}
} // namespace

KaeHandle::~KaeHandle() {
  if (polling_thread_ == nullptr) {
    libuadk_->kaeDelRsaCtx(rsa_ctx_);
    libuadk_->kaeBlkPoolDestory(mem_pool_);
    libuadk_->kaeStopInstance(handle_);
    delete handle_;
    return;
  }

  {
    Thread::LockGuard poll_lock(poll_lock_);
    done_ = true;
    kae_thread_cond_.notifyOne();
  }
  polling_thread_->join();

  libuadk_->kaeDelRsaCtx(rsa_ctx_);
  libuadk_->kaeBlkPoolDestory(mem_pool_);
  libuadk_->kaeStopInstance(handle_);
  delete handle_;
}

bool KaeHandle::initKaeInstance(LibUadkCryptoSharedPtr libuadk) {
  libuadk_ = libuadk;
  handle_ = new wd_queue;
  memset(static_cast<void*>(handle_), 0, sizeof(wd_queue));
  handle_->capa.alg = RSA_ALG;

  // Request KAE queue
  int ret = libuadk_->kaeRequestQueue(handle_);
  if (ret != WD_SUCCESS) {
    ENVOY_LOG(error, "kae request queue failed");
    return false;
  }

  // Init Mem Pool
  wd_blkpool_setup setup;
  memset(static_cast<void*>(&setup), 0, sizeof(wd_blkpool_setup));
  setup.block_size = RSA_BLOCK_SIZE;
  setup.block_num = RSA_BLOCK_NUM;
  setup.align_size = 64;
  mem_pool_ = libuadk_->kaeBlkPoolCreate(handle_, &setup);
  if (mem_pool_ == nullptr) {
    ENVOY_LOG(error, "create mem pool failed");
    return false;
  }

  // Init Rsa Ctx
  rsa_setup_.is_crt = true;
  rsa_setup_.cb = static_cast<wcrypto_cb>(rsaCb);
  rsa_setup_.key_bits = RSA2048BITS;
  rsa_setup_.br.alloc = kaeWdAllocBlk;
  rsa_setup_.br.free = kaeWdFreeBlk;
  rsa_setup_.br.iova_map = kaeDmaMap;
  rsa_setup_.br.iova_unmap = kaeDmaUnmap;
  rsa_setup_.br.usr = mem_pool_;
  rsa_ctx_ = libuadk_->kaeCreateRsaCtx(handle_, &rsa_setup_);
  if (rsa_ctx_ == nullptr) {
    ENVOY_LOG(error, "create rsa ctx failed");
    return false;
  }
  ENVOY_LOG(debug, "create Kae handle");

  return true;
}

void KaeHandle::setLibUadk(LibUadkCryptoSharedPtr libuadk) { libuadk_ = libuadk; }

LibUadkCryptoSharedPtr KaeHandle::getLibuadk() { return libuadk_; }

WdHandle KaeHandle::getHandle() { return handle_; }

void* KaeHandle::getRsaCtx() { return rsa_ctx_; }

wcrypto_rsa_ctx_setup& KaeHandle::getRsaCtxSetup() { return rsa_setup_; }

void* KaeHandle::getMemPool() { return mem_pool_; }

void KaeHandle::addUser() { users_++; }

void KaeHandle::removeUser() {
  ASSERT(users_ > 0);
  users_--;
}

bool KaeHandle::hasUsers() { return users_ > 0; }

bool KaeHandle::isDone() { return done_; }

// KAE Section
KaeSection::KaeSection(LibUadkCryptoSharedPtr libuadk) : libuadk_(libuadk) {};

bool KaeSection::startSection(Api::Api& api, std::chrono::milliseconds poll_delay,
                              uint32_t max_instances) {
  int ret = libuadk_->kaeGetNumInstances(&num_instances_);
  ENVOY_LOG(info, "found {} KAE instances", num_instances_);
  if (ret != WD_SUCCESS) {
    return false;
  }

  num_instances_ = std::min(num_instances_, max_instances);
  ENVOY_LOG(info, "use {} KAE instances", num_instances_);

  kae_handles_ = std::vector<KaeHandle>(num_instances_);

  for (int i = 0; i < static_cast<int>(num_instances_); i++) {
    if (!kae_handles_[i].initKaeInstance(libuadk_)) {
      return false;
    }

    // Every handle has a polling thread associated with it. This is needed
    // until libuadk implements event-based notifications when the KAE operation
    // is ready.
    kae_handles_[i].polling_thread_ =
        api.threadFactory().createThread([this, poll_delay, i]() -> void {
          KaeManager::kaePoll(this->kae_handles_[i], poll_delay);
        });
  }

  return true;
}

bool KaeSection::isInitialized() { return num_instances_ > 0; }

KaeHandle& KaeSection::getNextHandle() {
  Thread::LockGuard handle_lock(handle_lock_);
  if (next_handle_ == static_cast<int>(num_instances_)) {
    next_handle_ = 0;
  }

  return kae_handles_[next_handle_++];
}

// KAE Context
KaeContext::KaeContext(KaeHandle& handle) : handle_(handle) {}

KaeContext::~KaeContext() {
  if (read_fd_ >= 0) {
    close(read_fd_);
  }

  if (write_fd_ >= 0) {
    close(write_fd_);
  }
}

bool KaeContext::init() {
  int pipe_fds[2] = {0, 1};
  int ret = pipe(pipe_fds);

  if (ret == -1) {
    return false;
  }

  read_fd_ = pipe_fds[0];
  write_fd_ = pipe_fds[1];

  return true;
}

bool KaeContext::convertBnToFlatbuffer(wd_dtb* fb, const BIGNUM* bn) {
  fb->dsize = BN_num_bytes(bn);
  if (fb->dsize == 0) {
    return false;
  }

  if (fb->data == nullptr) {
    fb->dsize = 0;
    return false;
  }

  auto size = BN_bn2bin(bn, reinterpret_cast<unsigned char*>(fb->data));
  if (size == 0) {
    fb->dsize = 0;
    return false;
  }
  fb->dsize = size;

  return true;
}

void* KaeContext::allocBlk() {
  void* pool = getHandle().getMemPool();
  return getHandle().getRsaCtxSetup().br.alloc(pool, 0);
}

void KaeContext::freeBlk(void* blk) {
  void* pool = getHandle().getMemPool();
  getHandle().getRsaCtxSetup().br.free(pool, blk);
}

void KaeContext::freeDecryptOpBuf(wcrypto_rsa_op_data* op_data) {
  if (op_data) {
    if (op_data->in) {
      freeBlk(op_data->in);
    }
    if (op_data->out) {
      freeBlk(op_data->out);
    }
    OPENSSL_free(op_data);
  }
}

bool KaeContext::buildRsaOpBuf(int from_len, const unsigned char* from, RSA* rsa, int padding,
                               wcrypto_rsa_op_data** op_data) {

  const BIGNUM* p = nullptr;
  const BIGNUM* q = nullptr;
  RSA_get0_factors(rsa, &p, &q);

  const BIGNUM* dmp1 = nullptr;
  const BIGNUM* dmq1 = nullptr;
  const BIGNUM* iqmp = nullptr;
  RSA_get0_crt_params(rsa, &dmp1, &dmq1, &iqmp);

  if (!p || !q || !dmp1 || !dmq1 || !iqmp) {
    return false;
  }

  *op_data = static_cast<wcrypto_rsa_op_data*>(OPENSSL_malloc(sizeof(wcrypto_rsa_op_data)));
  if (*op_data == nullptr) {
    return false;
  }
  memset(*op_data, 0, sizeof(**op_data));

  wcrypto_rsa_prikey* prikey = nullptr;
  wd_dtb *wd_dq, *wd_dp, *wd_q, *wd_p, *wd_qinv;
  getLibuadk()->kaeGetRsaPrikey(getRsaCtx(), &prikey);
  getLibuadk()->kaeGetRsaCrtPrikeyParams(prikey, &wd_dq, &wd_dp, &wd_qinv, &wd_q, &wd_p);
  if (!convertBnToFlatbuffer(wd_p, p) || !convertBnToFlatbuffer(wd_q, q) ||
      !convertBnToFlatbuffer(wd_dp, dmp1) || !convertBnToFlatbuffer(wd_dq, dmq1) ||
      !convertBnToFlatbuffer(wd_qinv, iqmp)) {
    freeDecryptOpBuf(*op_data);
    return false;
  }

  int rsa_len = RSA_size(rsa);
  getHandle().getRsaCtxSetup().key_bits = rsa_len << BIT_BYTES_SHIFT;
  (*op_data)->in = allocBlk();
  if ((*op_data)->in == nullptr) {
    freeDecryptOpBuf(*op_data);
    return false;
  }
  (*op_data)->in_bytes = padding != RSA_NO_PADDING ? rsa_len : from_len;
  (*op_data)->op_type = WCRYPTO_RSA_SIGN;

  // Add RSA PKCS 1.5 padding if needed. The RSA PSS padding is already added.
  if (padding == RSA_PKCS1_PADDING) {
    if (rsa_len < from_len + 3 + 8) {
      freeDecryptOpBuf(*op_data);
      return false;
    }
    // PKCS1 1.5 padding from RFC 8017 9.2.
    int ff_padding_amount = rsa_len - from_len - 3;
    if (ff_padding_amount < 8) {
      freeDecryptOpBuf(*op_data);
      return false;
    }
    int idx = 0;
    uint8_t* in = static_cast<uint8_t*>((*op_data)->in);
    in[idx++] = 0;
    in[idx++] = 1;
    memset(in + idx, 0xff, ff_padding_amount);
    idx += ff_padding_amount;
    in[idx++] = 0;
    memcpy(in + idx, from, from_len); // NOLINT(safe-memcpy)
  } else if (padding == RSA_NO_PADDING) {
    if (from_len != rsa_len) {
      freeDecryptOpBuf(*op_data);
      return false;
    }
    memcpy((*op_data)->in, from, from_len); // NOLINT(safe-memcpy)
  } else {
    // Non-supported padding
    freeDecryptOpBuf(*op_data);
    return false;
  }

  (*op_data)->out = allocBlk();
  if ((*op_data)->out == nullptr) {
    freeDecryptOpBuf(*op_data);
    return false;
  }
  (*op_data)->out_bytes = rsa_len;

  return true;
}
bool KaeContext::decrypt(int len, const unsigned char* from, RSA* rsa, int padding) {

  int key_bits = RSA_bits(rsa);
  if (!checkBitUseful(key_bits)) {
    return false;
  }

  int ret = buildRsaOpBuf(len, from, rsa, padding, &op_data_);
  if (!ret) {
    return false;
  }

  int status;
  do {
    status = getLibuadk()->kaeDoRsa(handle_.getRsaCtx(), op_data_, this);
  } while (status == WD_STATUS_BUSY);

  if (status != WD_SUCCESS) {
    freeDecryptOpBuf(op_data_);
    return false;
  }

  {
    Thread::LockGuard poll_lock(handle_.poll_lock_);
    handle_.addUser();
    // Start polling for the result
    handle_.kae_thread_cond_.notifyOne();
  }

  return true;
}

bool KaeContext::checkBitUseful(const int bit) {
  switch (bit) {
  case RSA1024BITS:
  case RSA2048BITS:
  case RSA3072BITS:
  case RSA4096BITS:
    return true;
  default:
    break;
  }
  return false;
}

KaeHandle& KaeContext::getHandle() { return handle_; }

LibUadkCryptoSharedPtr KaeContext::getLibuadk() { return handle_.getLibuadk(); }

void* KaeContext::getRsaCtx() { return handle_.getRsaCtx(); }

wcrypto_rsa_op_data* KaeContext::getOpData() { return op_data_; }

int KaeContext::getDecryptedDataLength() { return decrypted_data_length_; }

unsigned char* KaeContext::getDecryptedData() { return decrypted_data_.data(); }

void KaeContext::setOpStatus(int status) { last_status_ = status; }

int KaeContext::getOpStatus() { return last_status_; }

bool KaeContext::copyDecryptedData(unsigned char* bytes, int len) {
  ASSERT(bytes != nullptr);
  if (len > KAE_BUFFER_SIZE) {
    return false;
  }

  memcpy(decrypted_data_.data(), bytes, len); // NOLINT(safe-memcpy)
  decrypted_data_length_ = len;
  return true;
}

int KaeContext::getFd() { return read_fd_; }

int KaeContext::getWriteFd() { return write_fd_; }

} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
