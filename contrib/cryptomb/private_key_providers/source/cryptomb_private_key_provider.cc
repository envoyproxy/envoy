#include "contrib/cryptomb/private_key_providers/source/cryptomb_private_key_provider.h"

#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/datasource.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace CryptoMb {

CryptoMbContext::CryptoMbContext(Event::Dispatcher& dispatcher,
                                 Ssl::PrivateKeyConnectionCallbacks& cb)
    : status_(RequestStatus::Retry), dispatcher_(dispatcher), cb_(cb) {}

void CryptoMbContext::scheduleCallback(enum RequestStatus status) {
  schedulable_ = dispatcher_.createSchedulableCallback([this, status]() {
    // The status can't be set beforehand, because the callback asserts
    // if someone else races to call doHandshake() and the status goes to
    // HandshakeComplete.
    setStatus(status);
    this->cb_.onPrivateKeyMethodComplete();
  });
  schedulable_->scheduleCallbackNextIteration();
}

bool CryptoMbEcdsaContext::ecdsaInit(const uint8_t* in, size_t in_len) {
  if (ec_key_ == nullptr) {
    return false;
  }

  const EC_GROUP* group = EC_KEY_get0_group(ec_key_.get());
  priv_key_ = EC_KEY_get0_private_key(ec_key_.get());
  if (group == nullptr || priv_key_ == nullptr) {
    return false;
  }

  const BIGNUM* order = EC_GROUP_get0_order(group);
  if (order == nullptr) {
    return false;
  }

  // Create an ephemeral key.
  ctx_ = bssl::UniquePtr<BN_CTX>(BN_CTX_new());
  if (ctx_ == nullptr) {
    return false;
  }
  BN_CTX_start(ctx_.get());
  k_ = BN_CTX_get(ctx_.get());
  if (!k_) {
    return false;
  }
  do {
    if (!BN_rand_range(k_, order)) {
      return false;
    }
  } while (BN_is_zero(k_));

  // Extent with zero paddings as CryptoMB expects in_buf_ being sign length.
  int len = BN_num_bits(order);
  size_t buf_len = (len + 7) / 8;
  if (8 * in_len < static_cast<unsigned long>(len)) {
    in_buf_ = std::make_unique<uint8_t[]>(buf_len);
    memcpy(in_buf_.get() + buf_len - in_len, in, in_len); // NOLINT(safe-memcpy)
  } else {
    in_buf_ = std::make_unique<uint8_t[]>(in_len);
    memcpy(in_buf_.get(), in, in_len); // NOLINT(safe-memcpy)
  }

  sig_len_ = ECDSA_size(ec_key_.get());
  if (sig_len_ > MAX_SIGNATURE_SIZE) {
    return false;
  }

  return true;
}

bool CryptoMbRsaContext::rsaInit(const uint8_t* in, size_t in_len) {
  if (rsa_ == nullptr) {
    return false;
  }

  // Initialize the values with the RSA key.
  size_t in_buf_size = in_len;
  out_len_ = RSA_size(rsa_.get());

  if (out_len_ > in_buf_size) {
    in_buf_size = out_len_;
  }

  RSA_get0_key(rsa_.get(), &n_, &e_, &d_);
  RSA_get0_factors(rsa_.get(), &p_, &q_);
  RSA_get0_crt_params(rsa_.get(), &dmp1_, &dmq1_, &iqmp_);

  if (p_ == nullptr || q_ == nullptr || dmp1_ == nullptr || dmq1_ == nullptr || iqmp_ == nullptr) {
    return false;
  }

  in_buf_ = std::make_unique<uint8_t[]>(in_buf_size);
  memcpy(in_buf_.get(), in, in_len); // NOLINT(safe-memcpy)

  return true;
}

namespace {

int calculateDigest(const EVP_MD* md, const uint8_t* in, size_t in_len, unsigned char* hash,
                    unsigned int* hash_len) {
  bssl::ScopedEVP_MD_CTX ctx;

  // Calculate the message digest for signing.
  if (!EVP_DigestInit_ex(ctx.get(), md, nullptr) || !EVP_DigestUpdate(ctx.get(), in, in_len) ||
      !EVP_DigestFinal_ex(ctx.get(), hash, hash_len)) {
    return 0;
  }
  return 1;
}

ssl_private_key_result_t ecdsaPrivateKeySignInternal(CryptoMbPrivateKeyConnection* ops, uint8_t*,
                                                     size_t*, size_t, uint16_t signature_algorithm,
                                                     const uint8_t* in, size_t in_len) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;

  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  const EVP_MD* md = SSL_get_signature_algorithm_digest(signature_algorithm);
  if (md == nullptr) {
    return ssl_private_key_failure;
  }

  if (!calculateDigest(md, in, in_len, hash, &hash_len)) {
    return ssl_private_key_failure;
  }

  bssl::UniquePtr<EVP_PKEY> pkey = ops->getPrivateKey();
  if (pkey == nullptr) {
    return ssl_private_key_failure;
  }

  if (EVP_PKEY_id(pkey.get()) != SSL_get_signature_algorithm_key_type(signature_algorithm)) {
    return ssl_private_key_failure;
  }

  bssl::UniquePtr<EC_KEY> ec_key(EVP_PKEY_get1_EC_KEY(pkey.get()));
  if (ec_key == nullptr) {
    return ssl_private_key_failure;
  }

  // Create MB context which will be used for this particular
  // signing/decryption.
  CryptoMbEcdsaContextSharedPtr mb_ctx =
      std::make_shared<CryptoMbEcdsaContext>(std::move(ec_key), ops->dispatcher_, ops->cb_);

  if (!mb_ctx->ecdsaInit(hash, hash_len)) {
    return ssl_private_key_failure;
  }

  ops->addToQueue(mb_ctx);
  return ssl_private_key_retry;
}

ssl_private_key_result_t ecdsaPrivateKeySign(SSL* ssl, uint8_t* out, size_t* out_len,
                                             size_t max_out, uint16_t signature_algorithm,
                                             const uint8_t* in, size_t in_len) {
  return ssl == nullptr ? ssl_private_key_failure
                        : ecdsaPrivateKeySignInternal(
                              static_cast<CryptoMbPrivateKeyConnection*>(SSL_get_ex_data(
                                  ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex())),
                              out, out_len, max_out, signature_algorithm, in, in_len);
}

ssl_private_key_result_t ecdsaPrivateKeyDecrypt(SSL*, uint8_t*, size_t*, size_t, const uint8_t*,
                                                size_t) {
  // Expecting to get only signing requests.
  return ssl_private_key_failure;
}

ssl_private_key_result_t ecdsaPrivateKeyCompleteInternal(CryptoMbPrivateKeyConnection* ops,
                                                         uint8_t* out, size_t* out_len,
                                                         size_t max_out) {
  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  // Check if the MB operation is ready yet. This can happen if someone calls
  // the top-level SSL function too early. The op status is only set from this
  // thread.
  if (ops->mb_ctx_->getStatus() == RequestStatus::Retry) {
    return ssl_private_key_retry;
  }

  // If this point is reached, the MB processing must be complete.

  // See if the operation failed.
  if (ops->mb_ctx_->getStatus() != RequestStatus::Success) {
    ops->logWarnMsg("private key operation failed.");
    return ssl_private_key_failure;
  }

  CryptoMbEcdsaContextSharedPtr mb_ctx =
      std::static_pointer_cast<CryptoMbEcdsaContext>(ops->mb_ctx_);
  if (mb_ctx->sig_len_ > max_out) {
    return ssl_private_key_failure;
  }

  ECDSA_SIG* sig = ECDSA_SIG_new();
  if (sig == nullptr) {
    return ssl_private_key_failure;
  }
  BIGNUM* sig_r = BN_bin2bn(mb_ctx->sig_r_, 32, nullptr);
  BIGNUM* sig_s = BN_bin2bn(mb_ctx->sig_s_, 32, nullptr);
  ECDSA_SIG_set0(sig, sig_r, sig_s);

  // Marshal signature into out.
  CBB cbb;
  if (!CBB_init_fixed(&cbb, out, mb_ctx->sig_len_) || !ECDSA_SIG_marshal(&cbb, sig) ||
      !CBB_finish(&cbb, nullptr, out_len)) {
    CBB_cleanup(&cbb);
    ECDSA_SIG_free(sig);
    return ssl_private_key_failure;
  }

  ECDSA_SIG_free(sig);

  return ssl_private_key_success;
}

ssl_private_key_result_t ecdsaPrivateKeyComplete(SSL* ssl, uint8_t* out, size_t* out_len,
                                                 size_t max_out) {
  return ssl == nullptr ? ssl_private_key_failure
                        : ecdsaPrivateKeyCompleteInternal(
                              static_cast<CryptoMbPrivateKeyConnection*>(SSL_get_ex_data(
                                  ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex())),
                              out, out_len, max_out);
}

ssl_private_key_result_t rsaPrivateKeySignInternal(CryptoMbPrivateKeyConnection* ops, uint8_t*,
                                                   size_t*, size_t, uint16_t signature_algorithm,
                                                   const uint8_t* in, size_t in_len) {

  ssl_private_key_result_t status = ssl_private_key_failure;
  if (ops == nullptr) {
    return status;
  }

  bssl::UniquePtr<EVP_PKEY> pkey = ops->getPrivateKey();

  // Check if the SSL instance has correct data attached to it.
  if (EVP_PKEY_id(pkey.get()) != SSL_get_signature_algorithm_key_type(signature_algorithm)) {
    return status;
  }

  bssl::UniquePtr<RSA> rsa(EVP_PKEY_get1_RSA(pkey.get()));
  if (rsa == nullptr) {
    return status;
  }

  const EVP_MD* md = SSL_get_signature_algorithm_digest(signature_algorithm);
  if (md == nullptr) {
    return status;
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;
  if (!calculateDigest(md, in, in_len, hash, &hash_len)) {
    return status;
  }

  uint8_t* msg;
  size_t msg_len;

  // Add RSA padding to the the hash. `PSS` and `PKCS#1` v1.5 padding schemes are supported.
  if (SSL_is_signature_algorithm_rsa_pss(signature_algorithm)) {
    msg_len = RSA_size(rsa.get());
    msg = static_cast<uint8_t*>(OPENSSL_malloc(msg_len));
    if (msg == nullptr) {
      return status;
    }

    if (!RSA_padding_add_PKCS1_PSS_mgf1(rsa.get(), msg, hash, md, nullptr, -1)) {
      OPENSSL_free(msg);
      return status;
    }
  } else {
    // PKCS#1 1.5
    int prefix_allocated = 0;
    if (!RSA_add_pkcs1_prefix(&msg, &msg_len, &prefix_allocated, EVP_MD_type(md), hash, hash_len)) {
      if (prefix_allocated) {
        OPENSSL_free(msg);
      }
      return status;
    }

    // RFC 8017 section 9.2

    unsigned long rsa_len = RSA_size(rsa.get());
    // Header is 3 bytes, padding is min 8 bytes
    unsigned long header_len = 3;
    unsigned long padding_len = rsa_len - msg_len - header_len;

    if (padding_len < 8) {
      OPENSSL_free(msg);
      return status;
    }

    uint8_t* full_msg = static_cast<uint8_t*>(OPENSSL_malloc(rsa_len));
    if (full_msg == nullptr) {
      if (prefix_allocated) {
        OPENSSL_free(msg);
      }
      return status;
    }

    int idx = 0;
    full_msg[idx++] = 0x0;                     // first header byte
    full_msg[idx++] = 0x1;                     // second header byte
    memset(full_msg + idx, 0xff, padding_len); // padding
    idx += padding_len;
    full_msg[idx++] = 0x0;                // third header byte
    memcpy(full_msg + idx, msg, msg_len); // NOLINT(safe-memcpy)

    if (prefix_allocated) {
      OPENSSL_free(msg);
    }

    msg = full_msg;
    msg_len = rsa_len;
  }

  // Create MB context which will be used for this particular
  // signing/decryption.
  CryptoMbRsaContextSharedPtr mb_ctx =
      std::make_shared<CryptoMbRsaContext>(std::move(pkey), ops->dispatcher_, ops->cb_);

  if (!mb_ctx->rsaInit(msg, msg_len)) {
    OPENSSL_free(msg);
    return status;
  }

  ops->addToQueue(mb_ctx);
  status = ssl_private_key_retry;
  OPENSSL_free(msg);
  return status;
}

ssl_private_key_result_t rsaPrivateKeySign(SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out,
                                           uint16_t signature_algorithm, const uint8_t* in,
                                           size_t in_len) {
  return ssl == nullptr ? ssl_private_key_failure
                        : rsaPrivateKeySignInternal(
                              static_cast<CryptoMbPrivateKeyConnection*>(SSL_get_ex_data(
                                  ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex())),
                              out, out_len, max_out, signature_algorithm, in, in_len);
}

ssl_private_key_result_t rsaPrivateKeyDecryptInternal(CryptoMbPrivateKeyConnection* ops, uint8_t*,
                                                      size_t*, size_t, const uint8_t* in,
                                                      size_t in_len) {

  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  bssl::UniquePtr<EVP_PKEY> pkey = ops->getPrivateKey();

  // Check if the SSL instance has correct data attached to it.
  if (pkey == nullptr) {
    return ssl_private_key_failure;
  }

  CryptoMbRsaContextSharedPtr mb_ctx =
      std::make_shared<CryptoMbRsaContext>(std::move(pkey), ops->dispatcher_, ops->cb_);

  if (!mb_ctx->rsaInit(in, in_len)) {
    return ssl_private_key_failure;
  }

  ops->addToQueue(mb_ctx);
  return ssl_private_key_retry;
}

ssl_private_key_result_t rsaPrivateKeyDecrypt(SSL* ssl, uint8_t* out, size_t* out_len,
                                              size_t max_out, const uint8_t* in, size_t in_len) {
  return ssl == nullptr ? ssl_private_key_failure
                        : rsaPrivateKeyDecryptInternal(
                              static_cast<CryptoMbPrivateKeyConnection*>(SSL_get_ex_data(
                                  ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex())),
                              out, out_len, max_out, in, in_len);
}

ssl_private_key_result_t rsaPrivateKeyCompleteInternal(CryptoMbPrivateKeyConnection* ops,
                                                       uint8_t* out, size_t* out_len,
                                                       size_t max_out) {
  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  // Check if the MB operation is ready yet. This can happen if someone calls
  // the top-level SSL function too early. The op status is only set from this
  // thread.
  if (ops->mb_ctx_->getStatus() == RequestStatus::Retry) {
    return ssl_private_key_retry;
  }

  // If this point is reached, the MB processing must be complete.

  // See if the operation failed.
  if (ops->mb_ctx_->getStatus() != RequestStatus::Success) {
    ops->logWarnMsg("private key operation failed.");
    return ssl_private_key_failure;
  }

  CryptoMbRsaContextSharedPtr mb_ctx = std::static_pointer_cast<CryptoMbRsaContext>(ops->mb_ctx_);
  *out_len = mb_ctx->out_len_;

  if (*out_len > max_out) {
    return ssl_private_key_failure;
  }

  memcpy(out, mb_ctx->out_buf_, *out_len); // NOLINT(safe-memcpy)

  return ssl_private_key_success;
}

ssl_private_key_result_t rsaPrivateKeyComplete(SSL* ssl, uint8_t* out, size_t* out_len,
                                               size_t max_out) {
  return ssl == nullptr ? ssl_private_key_failure
                        : rsaPrivateKeyCompleteInternal(
                              static_cast<CryptoMbPrivateKeyConnection*>(SSL_get_ex_data(
                                  ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex())),
                              out, out_len, max_out);
}

} // namespace

// External linking, meant for testing without SSL context.
ssl_private_key_result_t ecdsaPrivateKeyCompleteForTest(CryptoMbPrivateKeyConnection* ops,
                                                        uint8_t* out, size_t* out_len,
                                                        size_t max_out) {
  return ecdsaPrivateKeyCompleteInternal(ops, out, out_len, max_out);
}
ssl_private_key_result_t rsaPrivateKeyCompleteForTest(CryptoMbPrivateKeyConnection* ops,
                                                      uint8_t* out, size_t* out_len,
                                                      size_t max_out) {
  return rsaPrivateKeyCompleteInternal(ops, out, out_len, max_out);
}
ssl_private_key_result_t ecdsaPrivateKeySignForTest(CryptoMbPrivateKeyConnection* ops, uint8_t* out,
                                                    size_t* out_len, size_t max_out,
                                                    uint16_t signature_algorithm, const uint8_t* in,
                                                    size_t in_len) {
  return ecdsaPrivateKeySignInternal(ops, out, out_len, max_out, signature_algorithm, in, in_len);
}
ssl_private_key_result_t rsaPrivateKeySignForTest(CryptoMbPrivateKeyConnection* ops, uint8_t* out,
                                                  size_t* out_len, size_t max_out,
                                                  uint16_t signature_algorithm, const uint8_t* in,
                                                  size_t in_len) {
  return rsaPrivateKeySignInternal(ops, out, out_len, max_out, signature_algorithm, in, in_len);
}
ssl_private_key_result_t rsaPrivateKeyDecryptForTest(CryptoMbPrivateKeyConnection* ops,
                                                     uint8_t* out, size_t* out_len, size_t max_out,
                                                     const uint8_t* in, size_t in_len) {
  return rsaPrivateKeyDecryptInternal(ops, out, out_len, max_out, in, in_len);
}

CryptoMbQueue::CryptoMbQueue(std::chrono::milliseconds poll_delay, enum KeyType type, int keysize,
                             IppCryptoSharedPtr ipp, Event::Dispatcher& d, CryptoMbStats& stats)
    : us_(std::chrono::duration_cast<std::chrono::microseconds>(poll_delay)), type_(type),
      key_size_(keysize), ipp_(ipp), timer_(d.createTimer([this]() { processRequests(); })),
      stats_(stats) {
  request_queue_.reserve(MULTIBUFF_BATCH);
}

void CryptoMbQueue::startTimer() { timer_->enableHRTimer(us_); }

void CryptoMbQueue::stopTimer() { timer_->disableTimer(); }

void CryptoMbQueue::addAndProcessEightRequests(CryptoMbContextSharedPtr mb_ctx) {
  // Add the request to the processing queue.
  ASSERT(request_queue_.size() < MULTIBUFF_BATCH);
  request_queue_.push_back(mb_ctx);

  if (request_queue_.size() == MULTIBUFF_BATCH) {
    // There are eight requests in the queue and we can process them.
    stopTimer();
    ENVOY_LOG(debug, "processing directly 8 requests");
    processRequests();
  } else if (request_queue_.size() == 1) {
    // First request in the queue, start the queue timer.
    startTimer();
  }
}

void CryptoMbQueue::processRequests() {
  switch (type_) {
  case KeyType::Rsa:
    // Record queue size statistic value for histogram.
    stats_.rsa_queue_sizes_.recordValue(request_queue_.size());
    processRsaRequests();
    break;
  case KeyType::Ec:
    // Record queue size statistic value for histogram.
    stats_.ecdsa_queue_sizes_.recordValue(request_queue_.size());
    processEcdsaRequests();
  }
  request_queue_.clear();
}

void CryptoMbQueue::processRsaRequests() {

  const unsigned char* rsa_priv_from[MULTIBUFF_BATCH] = {nullptr};
  unsigned char* rsa_priv_to[MULTIBUFF_BATCH] = {nullptr};
  const BIGNUM* rsa_lenstra_e[MULTIBUFF_BATCH] = {nullptr};
  const BIGNUM* rsa_lenstra_n[MULTIBUFF_BATCH] = {nullptr};
  const BIGNUM* rsa_priv_p[MULTIBUFF_BATCH] = {nullptr};
  const BIGNUM* rsa_priv_q[MULTIBUFF_BATCH] = {nullptr};
  const BIGNUM* rsa_priv_dmp1[MULTIBUFF_BATCH] = {nullptr};
  const BIGNUM* rsa_priv_dmq1[MULTIBUFF_BATCH] = {nullptr};
  const BIGNUM* rsa_priv_iqmp[MULTIBUFF_BATCH] = {nullptr};

  /* Build arrays of pointers for call */
  for (unsigned req_num = 0; req_num < request_queue_.size(); req_num++) {
    CryptoMbRsaContextSharedPtr mb_ctx =
        std::static_pointer_cast<CryptoMbRsaContext>(request_queue_[req_num]);
    rsa_priv_from[req_num] = mb_ctx->in_buf_.get();
    rsa_priv_to[req_num] = mb_ctx->out_buf_;
    rsa_priv_p[req_num] = mb_ctx->p_;
    rsa_priv_q[req_num] = mb_ctx->q_;
    rsa_priv_dmp1[req_num] = mb_ctx->dmp1_;
    rsa_priv_dmq1[req_num] = mb_ctx->dmq1_;
    rsa_priv_iqmp[req_num] = mb_ctx->iqmp_;
  }

  ENVOY_LOG(debug, "Multibuffer RSA process {} requests", request_queue_.size());

  uint32_t rsa_sts =
      ipp_->mbxRsaPrivateCrtSslMb8(rsa_priv_from, rsa_priv_to, rsa_priv_p, rsa_priv_q,
                                   rsa_priv_dmp1, rsa_priv_dmq1, rsa_priv_iqmp, key_size_);

  enum RequestStatus status[MULTIBUFF_BATCH] = {RequestStatus::Retry};

  for (unsigned req_num = 0; req_num < request_queue_.size(); req_num++) {
    CryptoMbRsaContextSharedPtr mb_ctx =
        std::static_pointer_cast<CryptoMbRsaContext>(request_queue_[req_num]);
    if (ipp_->mbxGetSts(rsa_sts, req_num)) {
      ENVOY_LOG(debug, "Multibuffer RSA request {} success", req_num);
      status[req_num] = RequestStatus::Success;
    } else {
      ENVOY_LOG(debug, "Multibuffer RSA request {} failure", req_num);
      status[req_num] = RequestStatus::Error;
    }

    // `Lenstra` check (validate that we get the same result back).
    rsa_priv_from[req_num] = rsa_priv_to[req_num];
    rsa_priv_to[req_num] = mb_ctx->lenstra_to_;
    rsa_lenstra_e[req_num] = mb_ctx->e_;
    rsa_lenstra_n[req_num] = mb_ctx->n_;
  }

  rsa_sts =
      ipp_->mbxRsaPublicSslMb8(rsa_priv_from, rsa_priv_to, rsa_lenstra_e, rsa_lenstra_n, key_size_);

  for (unsigned req_num = 0; req_num < request_queue_.size(); req_num++) {
    CryptoMbRsaContextSharedPtr mb_ctx =
        std::static_pointer_cast<CryptoMbRsaContext>(request_queue_[req_num]);
    enum RequestStatus ctx_status;
    if (ipp_->mbxGetSts(rsa_sts, req_num)) {
      if (CRYPTO_memcmp(mb_ctx->in_buf_.get(), rsa_priv_to[req_num], mb_ctx->out_len_) != 0) {
        status[req_num] = RequestStatus::Error;
      }
      // else keep the previous status from the private key operation
    } else {
      status[req_num] = RequestStatus::Error;
    }

    ctx_status = status[req_num];
    mb_ctx->scheduleCallback(ctx_status);
  }
}

void CryptoMbQueue::processEcdsaRequests() {
  uint8_t* pa_sig_r[MULTIBUFF_BATCH] = {};
  uint8_t* pa_sig_s[MULTIBUFF_BATCH] = {};
  const unsigned char* digest[MULTIBUFF_BATCH] = {nullptr};
  const BIGNUM* eph_key[MULTIBUFF_BATCH] = {nullptr};
  const BIGNUM* priv_key[MULTIBUFF_BATCH] = {nullptr};

  /* Build arrays of pointers for call */
  for (unsigned req_num = 0; req_num < request_queue_.size(); req_num++) {
    CryptoMbEcdsaContextSharedPtr mb_ctx =
        std::static_pointer_cast<CryptoMbEcdsaContext>(request_queue_[req_num]);
    pa_sig_r[req_num] = mb_ctx->sig_r_;
    pa_sig_s[req_num] = mb_ctx->sig_s_;
    digest[req_num] = mb_ctx->in_buf_.get();
    eph_key[req_num] = mb_ctx->k_;
    priv_key[req_num] = mb_ctx->priv_key_;
  }

  ENVOY_LOG(debug, "Multibuffer ECDSA process {} requests", request_queue_.size());

  uint32_t ecdsa_sts =
      ipp_->mbxNistp256EcdsaSignSslMb8(pa_sig_r, pa_sig_s, digest, eph_key, priv_key);

  enum RequestStatus status[MULTIBUFF_BATCH] = {RequestStatus::Retry};

  for (unsigned req_num = 0; req_num < request_queue_.size(); req_num++) {
    CryptoMbEcdsaContextSharedPtr mb_ctx =
        std::static_pointer_cast<CryptoMbEcdsaContext>(request_queue_[req_num]);
    enum RequestStatus ctx_status;
    if (ipp_->mbxGetSts(ecdsa_sts, req_num)) {
      ENVOY_LOG(debug, "Multibuffer ECDSA request {} success", req_num);
      status[req_num] = RequestStatus::Success;
    } else {
      ENVOY_LOG(debug, "Multibuffer ECDSA request {} failure", req_num);
      status[req_num] = RequestStatus::Error;
    }

    ctx_status = status[req_num];
    mb_ctx->scheduleCallback(ctx_status);

    // End context to invalid the ephemeral key.
    BN_CTX_end(mb_ctx->ctx_.get());
  }
}

CryptoMbPrivateKeyConnection::CryptoMbPrivateKeyConnection(Ssl::PrivateKeyConnectionCallbacks& cb,
                                                           Event::Dispatcher& dispatcher,
                                                           bssl::UniquePtr<EVP_PKEY> pkey,
                                                           CryptoMbQueue& queue)
    : queue_(queue), dispatcher_(dispatcher), cb_(cb), pkey_(std::move(pkey)) {}

void CryptoMbPrivateKeyMethodProvider::registerPrivateKeyMethod(
    SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher) {

  if (SSL_get_ex_data(ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex()) != nullptr) {
    throw EnvoyException("Not registering the CryptoMb provider twice for same context");
  }

  ASSERT(tls_->currentThreadRegistered(), "Current thread needs to be registered.");

  CryptoMbQueue& queue = tls_->get()->queue_;

  CryptoMbPrivateKeyConnection* ops =
      new CryptoMbPrivateKeyConnection(cb, dispatcher, bssl::UpRef(pkey_), queue);
  SSL_set_ex_data(ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex(), ops);
}

void CryptoMbPrivateKeyConnection::addToQueue(CryptoMbContextSharedPtr mb_ctx) {
  mb_ctx_ = mb_ctx;
  queue_.addAndProcessEightRequests(mb_ctx_);
}

bool CryptoMbPrivateKeyMethodProvider::checkFips() {
  // `ipp-crypto` library is not fips-certified at the moment
  // (https://github.com/intel/ipp-crypto#certification).
  return false;
}

bool CryptoMbPrivateKeyMethodProvider::isAvailable() { return initialized_; }

Ssl::BoringSslPrivateKeyMethodSharedPtr
CryptoMbPrivateKeyMethodProvider::getBoringSslPrivateKeyMethod() {
  return method_;
}

void CryptoMbPrivateKeyMethodProvider::unregisterPrivateKeyMethod(SSL* ssl) {
  CryptoMbPrivateKeyConnection* ops = static_cast<CryptoMbPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex()));
  SSL_set_ex_data(ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex(), nullptr);
  delete ops;
}

// The CryptoMbPrivateKeyMethodProvider is created on config.
CryptoMbPrivateKeyMethodProvider::CryptoMbPrivateKeyMethodProvider(
    const envoy::extensions::private_key_providers::cryptomb::v3alpha::
        CryptoMbPrivateKeyMethodConfig& conf,
    Server::Configuration::TransportSocketFactoryContext& factory_context, IppCryptoSharedPtr ipp)
    : api_(factory_context.serverFactoryContext().api()),
      tls_(ThreadLocal::TypedSlot<ThreadLocalData>::makeUnique(
          factory_context.serverFactoryContext().threadLocal())),
      stats_(generateCryptoMbStats("cryptomb", factory_context.statsScope())) {

  if (!ipp->mbxIsCryptoMbApplicable(0)) {
    ENVOY_LOG(warn, "Multi-buffer CPU instructions not available.");
    return;
  }

  std::chrono::milliseconds poll_delay =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(conf, poll_delay, 200));

  std::string private_key =
      THROW_OR_RETURN_VALUE(Config::DataSource::read(conf.private_key(), false, api_), std::string);

  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(const_cast<char*>(private_key.data()), private_key.size()));

  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    throw EnvoyException("Failed to read private key.");
  }

  method_ = std::make_shared<SSL_PRIVATE_KEY_METHOD>();

  int key_size;

  if (EVP_PKEY_id(pkey.get()) == EVP_PKEY_RSA) {
    ENVOY_LOG(debug, "CryptoMb key type: RSA");
    key_type_ = KeyType::Rsa;

    method_->sign = rsaPrivateKeySign;
    method_->decrypt = rsaPrivateKeyDecrypt;
    method_->complete = rsaPrivateKeyComplete;

    RSA* rsa = EVP_PKEY_get0_RSA(pkey.get());
    switch (RSA_bits(rsa)) {
    case 1024:
      key_size = 1024;
      break;
    case 2048:
      key_size = 2048;
      break;
    case 3072:
      key_size = 3072;
      break;
    case 4096:
      key_size = 4096;
      break;
    default:
      ENVOY_LOG(warn, "Only RSA keys of 1024, 2048, 3072, and 4096 bits are supported.");
      return;
    }

    // If longer keys are ever supported, remember to change the signature buffer to be larger.
    ASSERT(key_size / 8 <= CryptoMbContext::MAX_SIGNATURE_SIZE);

    BIGNUM e_check;
    // const BIGNUMs, memory managed by BoringSSL in RSA key structure.
    const BIGNUM* e = nullptr;
    const BIGNUM* n = nullptr;
    const BIGNUM* d = nullptr;
    RSA_get0_key(rsa, &n, &e, &d);
    BN_init(&e_check);
    BN_add_word(&e_check, 65537);
    if (e == nullptr || BN_ucmp(e, &e_check) != 0) {
      BN_free(&e_check);
      throw EnvoyException("Only RSA keys with \"e\" parameter value 65537 are allowed, because "
                           "we can validate the signatures using multi-buffer instructions.");
    }
    BN_free(&e_check);
  } else if (EVP_PKEY_id(pkey.get()) == EVP_PKEY_EC) {
    ENVOY_LOG(debug, "CryptoMb key type: ECDSA");
    key_type_ = KeyType::Ec;

    method_->sign = ecdsaPrivateKeySign;
    method_->decrypt = ecdsaPrivateKeyDecrypt;
    method_->complete = ecdsaPrivateKeyComplete;

    const EC_GROUP* ecdsa_group = EC_KEY_get0_group(EVP_PKEY_get0_EC_KEY(pkey.get()));
    if (ecdsa_group == nullptr) {
      throw EnvoyException("Invalid ECDSA key.");
    }
    BIGNUMConstPtr order(EC_GROUP_get0_order(ecdsa_group));
    if (EC_GROUP_get_curve_name(ecdsa_group) != NID_X9_62_prime256v1) {
      throw EnvoyException("Only P-256 ECDSA keys are supported.");
    }
    if (BN_num_bits(order.get()) < 160) {
      throw EnvoyException("Too few significant bits.");
    }
    key_size = EC_GROUP_get_degree(ecdsa_group);
    ASSERT(key_size == 256);
  } else {
    throw EnvoyException("Not supported key type, only EC and RSA are supported.");
  }

  pkey_ = std::move(pkey);

  enum KeyType key_type = key_type_;

  // Create a single queue for every worker thread to avoid locking.
  tls_->set([poll_delay, key_type, key_size, ipp, this](Event::Dispatcher& d) {
    ENVOY_LOG(debug, "Created CryptoMb Queue for thread {}", d.name());
    return std::make_shared<ThreadLocalData>(poll_delay, key_type, key_size, ipp, d, stats_);
  });

  initialized_ = true;
}

namespace {
int createIndex() {
  int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  RELEASE_ASSERT(index >= 0, "Failed to get SSL user data index.");
  return index;
}
} // namespace

int CryptoMbPrivateKeyMethodProvider::connectionIndex() {
  CONSTRUCT_ON_FIRST_USE(int, createIndex());
}

} // namespace CryptoMb
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
