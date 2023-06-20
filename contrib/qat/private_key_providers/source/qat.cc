#include "contrib/qat/private_key_providers/source/qat.h"

#include "libqat.h"
#include "openssl/rsa.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Qat {

QatManager::QatManager(LibQatCryptoSharedPtr libqat) {
  // Since we want to use VFs, it means that the section name doesn't
  // really matter but it needs to be a non-empty string. Use "SSL"
  // because that's what the qatlib tests use.

  libqat_ = libqat;
  CpaStatus status = libqat_->icpSalUserStart("SSL");
  if (status != CPA_STATUS_SUCCESS) {
    throw EnvoyException("Failed to start QAT device.");
  }
}

QatManager::~QatManager() {
  // The idea is that icp_sal_userStop() is called after the instances have been stopped and the
  // polling threads exited. Since QatManager is a singleton this is done only once.

  libqat_->icpSalUserStop();
}

void QatManager::qatPoll(QatHandle& handle, std::chrono::milliseconds poll_delay) {
  ENVOY_LOG(debug, "created QAT polling thread");
  while (1) {
    {
      Thread::LockGuard poll_lock(handle.poll_lock_);

      if (handle.isDone()) {
        return;
      }

      // Wait for an event which tells that a QAT request has been made.
      if (!handle.hasUsers()) {
        handle.qat_thread_cond_.wait(handle.poll_lock_);
      }
    }

    handle.getLibqat()->icpSalCyPollInstance(handle.getHandle(), 0);

    // Sleep for a while.
    std::this_thread::sleep_for(poll_delay); // NO_CHECK_FORMAT(real_time)
  }
  ENVOY_LOG(debug, "joined QAT polling thread");
}

namespace {
int createIndex() {
  int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  RELEASE_ASSERT(index >= 0, "Failed to get SSL user data index.");
  return index;
}
} // namespace

int QatManager::connectionIndex() { CONSTRUCT_ON_FIRST_USE(int, createIndex()); }

int QatManager::contextIndex() { CONSTRUCT_ON_FIRST_USE(int, createIndex()); }

QatHandle::~QatHandle() {
  if (polling_thread_ == nullptr) {
    return;
  }
  {
    Thread::LockGuard poll_lock(poll_lock_);
    done_ = true;
    qat_thread_cond_.notifyOne();
  }
  polling_thread_->join();

  libqat_->cpaCyStopInstance(handle_);
};

bool QatHandle::initQatInstance(CpaInstanceHandle handle, LibQatCryptoSharedPtr libqat) {
  libqat_ = libqat;
  handle_ = handle;

  CpaStatus status = libqat_->cpaCySetAddressTranslation(handle_, qaeVirtToPhysNUMA);
  if (status != CPA_STATUS_SUCCESS) {
    return false;
  }

  status = libqat_->cpaCyInstanceGetInfo2(handle_, &info_);
  if (status != CPA_STATUS_SUCCESS) {
    return false;
  }

  status = libqat_->cpaCyStartInstance(handle_);
  if (status != CPA_STATUS_SUCCESS) {
    return false;
  }

  return true;
}

void QatHandle::setLibqat(LibQatCryptoSharedPtr libqat) { libqat_ = libqat; }

LibQatCryptoSharedPtr QatHandle::getLibqat() { return libqat_; }

CpaInstanceHandle QatHandle::getHandle() { return handle_; }

void QatHandle::addUser() { users_++; };

void QatHandle::removeUser() {
  ASSERT(users_ > 0);
  users_--;
};

bool QatHandle::hasUsers() { return users_ > 0; };

int QatHandle::getNodeAffinity() { return info_.nodeAffinity; }

int QatHandle::isDone() { return done_; }

QatSection::QatSection(LibQatCryptoSharedPtr libqat) : libqat_(libqat){};

bool QatSection::startSection(Api::Api& api, std::chrono::milliseconds poll_delay) {

  // This function is called from a single-thread environment (server startup) to
  // initialize QAT for this particular section (or process name).

  CpaStatus status = libqat_->cpaCyGetNumInstances(&num_instances_);
  if ((status != CPA_STATUS_SUCCESS) || (num_instances_ <= 0)) {
    return false;
  }

  ENVOY_LOG(debug, "found {} QAT instances", num_instances_);

  CpaInstanceHandle* handles = new CpaInstanceHandle[num_instances_];

  status = libqat_->cpaCyGetInstances(num_instances_, handles);
  if (status != CPA_STATUS_SUCCESS) {
    delete[] handles;
    return false;
  }

  qat_handles_ = std::vector<QatHandle>(num_instances_);

  for (int i = 0; i < num_instances_; i++) {
    if (!qat_handles_[i].initQatInstance(handles[i], libqat_)) {
      delete[] handles;
      return false;
    }

    // Every handle has a polling thread associated with it. This is needed
    // until qatlib implements event-based notifications when the QAT operation
    // is ready.
    qat_handles_[i].polling_thread_ =
        api.threadFactory().createThread([this, poll_delay, i]() -> void {
          QatManager::qatPoll(this->qat_handles_[i], poll_delay);
        });
  }

  delete[] handles;

  return true;
}

bool QatSection::isInitialized() { return num_instances_ > 0; }

QatHandle& QatSection::getNextHandle() {
  Thread::LockGuard handle_lock(handle_lock_);
  if (next_handle_ == num_instances_) {
    next_handle_ = 0;
  }
  return qat_handles_[next_handle_++];
}

// The decrypt operation buffer creation functions are partially adapted from
// OpenSSL QAT engine. Changes include using the QAT library functions for
// allocating memory.

bool QatContext::convertBnToFlatbuffer(CpaFlatBuffer* fb, const BIGNUM* bn) {
  fb->dataLenInBytes = static_cast<Cpa32U>(BN_num_bytes(bn));
  if (fb->dataLenInBytes == 0) {
    fb->pData = nullptr;
    return false;
  }

  // Allocate continuous memory from the right NUMA node in 64 byte alignment.
  fb->pData = static_cast<Cpa8U*>(
      getLibqat()->qaeMemAllocNUMA(fb->dataLenInBytes, handle_.getNodeAffinity(), 64));
  if (fb->pData == nullptr) {
    fb->dataLenInBytes = 0;
    return false;
  }

  if (BN_bn2bin(bn, fb->pData) == 0) {
    fb->dataLenInBytes = 0;
    return false;
  }

  return true;
}

void QatContext::freeNuma(void* ptr) {
  if (ptr) {
    getLibqat()->qaeMemFreeNUMA(&ptr);
  }
}

void QatContext::freeDecryptOpBuf(CpaCyRsaDecryptOpData* dec_op_data, CpaFlatBuffer* out_buf) {
  CpaCyRsaPrivateKeyRep2* key = nullptr;

  if (dec_op_data) {
    if (dec_op_data->inputData.pData) {
      freeNuma(dec_op_data->inputData.pData);
    }

    if (dec_op_data->pRecipientPrivateKey) {
      key = &dec_op_data->pRecipientPrivateKey->privateKeyRep2;
      freeNuma(static_cast<void*>(key->prime1P.pData));
      freeNuma(static_cast<void*>(key->prime2Q.pData));
      freeNuma(static_cast<void*>(key->exponent1Dp.pData));
      freeNuma(static_cast<void*>(key->exponent2Dq.pData));
      freeNuma(static_cast<void*>(key->coefficientQInv.pData));
      OPENSSL_free(dec_op_data->pRecipientPrivateKey);
    }
    OPENSSL_free(dec_op_data);
  }

  if (out_buf) {
    if (out_buf->pData) {
      freeNuma(out_buf->pData);
    }
    OPENSSL_free(out_buf);
  }
}

bool QatContext::buildDecryptOpBuf(int from_len, const unsigned char* from, RSA* rsa, int padding,
                                   CpaCyRsaDecryptOpData** dec_op_data,
                                   CpaFlatBuffer** output_buffer) {
  const BIGNUM* p = nullptr;
  const BIGNUM* q = nullptr;
  RSA_get0_factors(rsa, &p, &q);

  const BIGNUM* dmp1 = nullptr;
  const BIGNUM* dmq1 = nullptr;
  const BIGNUM* iqmp = nullptr;
  RSA_get0_crt_params(rsa, &dmp1, &dmq1, &iqmp);

  if (p == nullptr || q == nullptr || dmp1 == nullptr || dmq1 == nullptr || iqmp == nullptr) {
    return false;
  }

  *dec_op_data = static_cast<CpaCyRsaDecryptOpData*>(OPENSSL_malloc(sizeof(CpaCyRsaDecryptOpData)));
  if (*dec_op_data == nullptr) {
    return false;
  }
  memset(*dec_op_data, 0, sizeof(**dec_op_data));

  CpaCyRsaPrivateKey* cpa_prv_key =
      static_cast<CpaCyRsaPrivateKey*>(OPENSSL_malloc(sizeof(CpaCyRsaPrivateKey)));
  if (cpa_prv_key == nullptr) {
    freeDecryptOpBuf(*dec_op_data, *output_buffer);
    return false;
  }
  memset(cpa_prv_key, 0, sizeof(*cpa_prv_key));

  (*dec_op_data)->pRecipientPrivateKey = cpa_prv_key;

  cpa_prv_key->version = CPA_CY_RSA_VERSION_TWO_PRIME;
  cpa_prv_key->privateKeyRepType = CPA_CY_RSA_PRIVATE_KEY_REP_TYPE_2;
  if (!convertBnToFlatbuffer(&cpa_prv_key->privateKeyRep2.prime1P, p) ||
      !convertBnToFlatbuffer(&cpa_prv_key->privateKeyRep2.prime2Q, q) ||
      !convertBnToFlatbuffer(&cpa_prv_key->privateKeyRep2.exponent1Dp, dmp1) ||
      !convertBnToFlatbuffer(&cpa_prv_key->privateKeyRep2.exponent2Dq, dmq1) ||
      !convertBnToFlatbuffer(&cpa_prv_key->privateKeyRep2.coefficientQInv, iqmp)) {
    freeDecryptOpBuf(*dec_op_data, *output_buffer);
    return false;
  }

  int rsa_len = RSA_size(rsa);

  (*dec_op_data)->inputData.pData = static_cast<Cpa8U*>(getLibqat()->qaeMemAllocNUMA(
      padding != RSA_NO_PADDING ? rsa_len : from_len, handle_.getNodeAffinity(), 64));

  if ((*dec_op_data)->inputData.pData == nullptr) {
    freeDecryptOpBuf(*dec_op_data, *output_buffer);
    return false;
  }

  (*dec_op_data)->inputData.dataLenInBytes = padding != RSA_NO_PADDING ? rsa_len : from_len;

  // Add RSA PKCS 1.5 padding if needed. The RSA PSS padding is already added.
  if (padding == RSA_PKCS1_PADDING) {
    if (rsa_len < from_len + 3 + 8) {
      freeDecryptOpBuf(*dec_op_data, *output_buffer);
      return false;
    }
    // PKCS1 1.5 padding from RFC 8017 9.2.
    int ff_padding_amount = rsa_len - from_len - 3;
    if (ff_padding_amount < 8) {
      freeDecryptOpBuf(*dec_op_data, *output_buffer);
      return false;
    }
    int idx = 0;
    (*dec_op_data)->inputData.pData[idx++] = 0;
    (*dec_op_data)->inputData.pData[idx++] = 1;
    memset((*dec_op_data)->inputData.pData + idx, 0xff, ff_padding_amount);
    idx += ff_padding_amount;
    (*dec_op_data)->inputData.pData[idx++] = 0;
    memcpy((*dec_op_data)->inputData.pData + idx, from, from_len); // NOLINT(safe-memcpy)
  } else if (padding == RSA_NO_PADDING) {
    // If there is no padding to be added, the input data needs to be the right size.
    if (from_len != rsa_len) {
      freeDecryptOpBuf(*dec_op_data, *output_buffer);
      return false;
    }
    memcpy((*dec_op_data)->inputData.pData, from, from_len); // NOLINT(safe-memcpy)
  } else {
    // Non-supported padding.
    freeDecryptOpBuf(*dec_op_data, *output_buffer);
    return false;
  }

  *output_buffer = static_cast<CpaFlatBuffer*>(OPENSSL_malloc(sizeof(CpaFlatBuffer)));
  if (*output_buffer == nullptr) {
    freeDecryptOpBuf(*dec_op_data, *output_buffer);
    return false;
  }
  memset(*output_buffer, 0, sizeof(CpaFlatBuffer));

  (*output_buffer)->pData =
      static_cast<Cpa8U*>(qaeMemAllocNUMA(rsa_len, handle_.getNodeAffinity(), 64));
  if ((*output_buffer)->pData == nullptr) {
    freeDecryptOpBuf(*dec_op_data, *output_buffer);
    return false;
  }
  (*output_buffer)->dataLenInBytes = rsa_len;

  return true;
}

namespace {
static void decryptCb(void* callback_tag, CpaStatus status, void* data, CpaFlatBuffer* out_buf) {
  // This function is called from the polling thread context as a result of polling indicating that
  // the QAT result is ready.
  QatContext* ctx = static_cast<QatContext*>(callback_tag);
  CpaCyRsaDecryptOpData* op_data = static_cast<CpaCyRsaDecryptOpData*>(data);

  ASSERT(ctx != nullptr);

  QatHandle& handle = ctx->getHandle();
  {
    Thread::LockGuard poll_lock(handle.poll_lock_);
    handle.removeUser();
  }
  {
    Thread::LockGuard data_lock(ctx->data_lock_);
    if (!ctx->copyDecryptedData(out_buf->pData, out_buf->dataLenInBytes)) {
      status = CPA_STATUS_FAIL;
    }

    ctx->freeDecryptOpBuf(op_data, out_buf);

    // Take the fd from the ctx and send the status to it. This indicates that the
    // decryption has completed and the upper layer can redo the SSL request.

    // TODO(ipuustin): OS system calls.
    int ret = write(ctx->getWriteFd(), &status, sizeof(status));
    UNREFERENCED_PARAMETER(ret);
  }
}
} // namespace

QatContext::QatContext(QatHandle& handle)
    : handle_(handle), last_status_(CPA_STATUS_RETRY), decrypted_data_length_(0), read_fd_(-1),
      write_fd_(-1) {}

QatContext::~QatContext() {
  if (read_fd_ >= 0) {
    close(read_fd_);
  }
  if (write_fd_ >= 0) {
    close(write_fd_);
  }
}

bool QatContext::init() {
  // The pipe is the communications channel from the polling thread to the worker thread.

  // TODO(ipuustin): OS system calls.
  int pipe_fds[2] = {0, 0};
  int ret = pipe(pipe_fds);

  if (ret == -1) {
    return false;
  }

  read_fd_ = pipe_fds[0];
  write_fd_ = pipe_fds[1];

  return true;
}

bool QatContext::decrypt(int len, const unsigned char* from, RSA* rsa, int padding) {
  CpaCyRsaDecryptOpData* op_data = nullptr;
  CpaFlatBuffer* out_buf = nullptr;

  // TODO(ipuustin): should this rather be a class function?
  int ret = buildDecryptOpBuf(len, from, rsa, padding, &op_data, &out_buf);
  if (!ret) {
    return false;
  }

  CpaStatus status;
  do {
    status = getLibqat()->cpaCyRsaDecrypt(handle_.getHandle(), decryptCb, this, op_data, out_buf);
  } while (status == CPA_STATUS_RETRY);

  if (status != CPA_STATUS_SUCCESS) {
    return false;
  }

  {
    Thread::LockGuard poll_lock(handle_.poll_lock_);
    handle_.addUser();
    // Start polling for the result.
    handle_.qat_thread_cond_.notifyOne();
  }

  return true;
}

QatHandle& QatContext::getHandle() { return handle_; };

LibQatCryptoSharedPtr QatContext::getLibqat() { return handle_.getLibqat(); };

int QatContext::getDecryptedDataLength() { return decrypted_data_length_; }

unsigned char* QatContext::getDecryptedData() { return decrypted_data_; }

void QatContext::setOpStatus(CpaStatus status) { last_status_ = status; };

CpaStatus QatContext::getOpStatus() { return last_status_; }

bool QatContext::copyDecryptedData(unsigned char* bytes, int len) {
  ASSERT(bytes != nullptr);
  if (len > QAT_BUFFER_SIZE) {
    return false;
  }
  memcpy(decrypted_data_, bytes, len); // NOLINT(safe-memcpy)
  decrypted_data_length_ = len;
  return true;
};

int QatContext::getFd() { return read_fd_; }

int QatContext::getWriteFd() { return write_fd_; };

} // namespace Qat
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
