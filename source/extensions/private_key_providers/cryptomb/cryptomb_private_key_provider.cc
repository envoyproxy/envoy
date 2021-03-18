#include "source/extensions/private_key_providers/cryptomb/cryptomb_private_key_provider.h"

#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "openssl/ec.h"
#include "openssl/ssl.h"

// BoringSSL internal definitions, needed for calculating ECDSA ephemeral key.
// TODO(ipuustin): ask BoringSSL maintainers to expose these in the BoringSSL headers?
#define BN_BYTES 8
#define EC_MAX_BYTES 66
#define EC_MAX_WORDS ((EC_MAX_BYTES + BN_BYTES - 1) / BN_BYTES)

typedef union {
  uint8_t bytes[EC_MAX_BYTES];
  BN_ULONG words[EC_MAX_WORDS];
} EC_SCALAR;

extern "C" {
int ec_random_nonzero_scalar(const EC_GROUP* group, EC_SCALAR* out,
                             const uint8_t additional_data[32]);
void ec_scalar_to_bytes(const EC_GROUP* group, uint8_t* out, size_t* out_len, const EC_SCALAR* in);
} // extern "C"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {

CryptoMbContext::CryptoMbContext(Event::Dispatcher& dispatcher,
                                 Ssl::PrivateKeyConnectionCallbacks& cb)
    : status_(RequestStatus::retry), dispatcher_(dispatcher), cb_(cb) {}

void CryptoMbContext::scheduleCallback(enum RequestStatus status) {
  schedulable_ = dispatcher_.createSchedulableCallback([this, status]() -> void {
    // The status can't be set beforehand, because the callback asserts
    // if some else races to call doHandshake() and the status goes to
    // HandshakeComplete.
    this->status_ = status;
    this->cb_.onPrivateKeyMethodComplete();
  });
  schedulable_->scheduleCallbackNextIteration();
}

bool CryptoMbEcdsaContext::ecdsaInit(EC_KEY* ec, const uint8_t* in, size_t in_len) {
  const BIGNUM* priv_key = EC_KEY_get0_private_key(ec);
  uint8_t* key_bytes = static_cast<uint8_t*>(OPENSSL_malloc(BN_num_bytes(priv_key)));

  BN_bn2bin(priv_key, key_bytes);

  // Calculate hash of the private key and the message to be signed.
  uint8_t additional_data[SHA512_DIGEST_LENGTH] = {0};
  SHA512_CTX sha;
  SHA512_Init(&sha);
  SHA512_Update(&sha, key_bytes, BN_num_bytes(priv_key));
  SHA512_Update(&sha, in, in_len);
  SHA512_Final(additional_data, &sha);

  OPENSSL_free(key_bytes);

  // Make the ephemeral key "k" to be a random number in the group, using the
  // hash as additional data. This is made to closely follow the way how
  // BoringSSL does this in order to not to implement any new cryptography. See
  // also https://tools.ietf.org/html/rfc6979#section-3.2

  EC_SCALAR k;
  const EC_GROUP* group = EC_KEY_get0_group(ec);
  if (!ec_random_nonzero_scalar(group, &k, additional_data)) {
    return false;
  } else {
    const BIGNUM* order = EC_GROUP_get0_order(group);
    uint8_t* k_bytes = static_cast<uint8_t*>(OPENSSL_malloc(BN_num_bytes(order)));
    size_t k_len;
    // Convert the scalar first to bytes...
    ec_scalar_to_bytes(group, k_bytes, &k_len, &k);
    // ... and then to BIGNUM to be usable in the multi-buffer function.
    BN_bin2bn(k_bytes, k_len, &k_);
    OPENSSL_free(k_bytes);
  }

  s_ = priv_key;

  in_buf_ = std::make_unique<uint8_t[]>(in_len);
  memcpy(in_buf_.get(), in, in_len); // NOLINT(safe-memcpy)

  ecdsa_sig_size_ = ECDSA_size(ec);

  return true;
}

bool CryptoMbRsaContext::rsaInit(RSA* rsa, const uint8_t* in, size_t in_len) {
  // Initialize the values with the RSA key.
  size_t in_buf_size = in_len;
  out_len_ = RSA_size(rsa);

  if (out_len_ > in_buf_size) {
    in_buf_size = out_len_;
  }

  RSA_get0_key(static_cast<RSA*>(rsa), &n_, &e_, &d_);
  RSA_get0_factors(static_cast<RSA*>(rsa), &p_, &q_);
  RSA_get0_crt_params(static_cast<RSA*>(rsa), &dmp1_, &dmq1_, &iqmp_);

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

ssl_private_key_result_t ecdsaPrivateKeySign(SSL* ssl, uint8_t*, size_t*, size_t,
                                             uint16_t signature_algorithm, const uint8_t* in,
                                             size_t in_len) {
  CryptoMbPrivateKeyConnection* ops = static_cast<CryptoMbPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex()));

  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  const EVP_MD* md = SSL_get_signature_algorithm_digest(signature_algorithm);
  if (md == nullptr) {
    return ssl_private_key_failure;
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;
  if (!calculateDigest(md, in, in_len, hash, &hash_len)) {
    ops->logWarnMsg("failed to calculate message digest.");
    return ssl_private_key_failure;
  }

  EVP_PKEY* pkey = ops->getPrivateKey();
  if (EVP_PKEY_id(pkey) != SSL_get_signature_algorithm_key_type(signature_algorithm)) {
    ops->logWarnMsg("wrong signature algorithm key type.");
    return ssl_private_key_failure;
  }

  bssl::UniquePtr<EC_KEY> ec_key(EVP_PKEY_get1_EC_KEY(pkey));
  if (ec_key == nullptr) {
    ops->logWarnMsg("no valid EC key.");
    return ssl_private_key_failure;
  }

  // Create MB context which will be used for this particular
  // signing/decryption.
  CryptoMbEcdsaContextSharedPtr mb_ctx =
      std::make_shared<CryptoMbEcdsaContext>(ops->dispatcher_, ops->cb_);

  if (!mb_ctx->ecdsaInit(ec_key.get(), hash, hash_len)) {
    ops->logWarnMsg("initializing the multibuffer context failed.");
    return ssl_private_key_failure;
  }

  ops->addToQueue(mb_ctx);

  return ssl_private_key_retry;
}

ssl_private_key_result_t ecdsaPrivateKeyDecrypt(SSL*, uint8_t*, size_t*, size_t, const uint8_t*,
                                                size_t) {
  // Expecting to get only signing requests.
  return ssl_private_key_failure;
}

ssl_private_key_result_t rsaPrivateKeySign(SSL* ssl, uint8_t*, size_t*, size_t,
                                           uint16_t signature_algorithm, const uint8_t* in,
                                           size_t in_len) {

  ssl_private_key_result_t status = ssl_private_key_failure;
  CryptoMbPrivateKeyConnection* ops = static_cast<CryptoMbPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex()));

  if (ops == nullptr) {
    return status;
  }

  EVP_PKEY* rsa_pkey = ops->getPrivateKey();
  // Check if the SSL instance has correct data attached to it.
  if (EVP_PKEY_id(rsa_pkey) != SSL_get_signature_algorithm_key_type(signature_algorithm)) {
    ops->logWarnMsg("wrong signature algorithm key type.");
    return status;
  }

  RSA* rsa = EVP_PKEY_get0_RSA(rsa_pkey);
  if (rsa == nullptr) {
    ops->logWarnMsg("not RSA key.");
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
  int prefix_allocated = 0;

  // Add RSA padding to the the hash. Supported types are PSS and PKCS1.
  if (SSL_is_signature_algorithm_rsa_pss(signature_algorithm)) {
    msg_len = RSA_size(rsa);
    msg = static_cast<uint8_t*>(OPENSSL_malloc(msg_len));
    if (msg == nullptr) {
      ops->logWarnMsg("failed to add RSA padding.");
      return status;
    }
    prefix_allocated = 1;
    if (!RSA_padding_add_PKCS1_PSS_mgf1(rsa, msg, hash, md, nullptr, -1)) {
      ops->logWarnMsg("failed to add RSA PSS padding.");
      if (prefix_allocated) {
        OPENSSL_free(msg);
      }
      return status;
    }
  } else {
    if (!RSA_add_pkcs1_prefix(&msg, &msg_len, &prefix_allocated, EVP_MD_type(md), hash, hash_len)) {
      ops->logWarnMsg("failed to add RSA PKCS1 padding.");
      if (prefix_allocated) {
        OPENSSL_free(msg);
      }
      return status;
    }
  }

  // Create MB context which will be used for this particular
  // signing/decryption.
  CryptoMbRsaContextSharedPtr mb_ctx =
      std::make_shared<CryptoMbRsaContext>(ops->dispatcher_, ops->cb_);

  if (!mb_ctx->rsaInit(rsa, msg, msg_len)) {
    ops->logWarnMsg("initializing the multibuffer context failed.");
    if (prefix_allocated) {
      OPENSSL_free(msg);
    }
    return status;
  }

  ops->addToQueue(mb_ctx);

  status = ssl_private_key_retry;

  // We will be called when some other thread or timer processes this request
  // from the queue.
  if (prefix_allocated) {
    OPENSSL_free(msg);
  }
  return status;
}

ssl_private_key_result_t rsaPrivateKeyDecrypt(SSL* ssl, uint8_t*, size_t*, size_t,
                                              const uint8_t* in, size_t in_len) {
  CryptoMbPrivateKeyConnection* ops = static_cast<CryptoMbPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex()));

  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  EVP_PKEY* rsa_pkey = ops->getPrivateKey();

  // Check if the SSL instance has correct data attached to it.
  if (rsa_pkey == nullptr) {
    ops->logWarnMsg("no valid key.");
    return ssl_private_key_failure;
  }

  RSA* rsa = EVP_PKEY_get0_RSA(rsa_pkey);
  if (rsa == nullptr) {
    ops->logWarnMsg("not RSA key.");
    return ssl_private_key_failure;
  }

  CryptoMbRsaContextSharedPtr mb_ctx =
      std::make_shared<CryptoMbRsaContext>(ops->dispatcher_, ops->cb_);

  if (!mb_ctx->rsaInit(rsa, in, in_len)) {
    ops->logWarnMsg("initializing the multibuffer context failed.");
    return ssl_private_key_failure;
  }

  ops->addToQueue(mb_ctx);

  return ssl_private_key_retry;
}

ssl_private_key_result_t privateKeyComplete(SSL* ssl, uint8_t* out, size_t* out_len,
                                            size_t max_out) {

  CryptoMbPrivateKeyConnection* ops = static_cast<CryptoMbPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex()));

  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  // Check if the MB operation is ready yet. This can happen if someone calls
  // the top-level SSL function too early. The op status is only set from this
  // thread.
  if (ops->mb_ctx_->getStatus() == RequestStatus::retry) {
    return ssl_private_key_retry;
  }

  // If this point is reached, the MB processing must be complete.

  // See if the operation failed.
  if (ops->mb_ctx_->getStatus() != RequestStatus::success) {
    ops->logWarnMsg("private key operation failed.");
    return ssl_private_key_failure;
  }

  *out_len = ops->mb_ctx_->out_len_;

  if (*out_len > max_out) {
    ops->logWarnMsg("too long output message.");
    return ssl_private_key_failure;
  }

  memcpy(out, ops->mb_ctx_->out_buf_, *out_len); // NOLINT(safe-memcpy)

  return ssl_private_key_success;
}

} // namespace

CryptoMbQueue::CryptoMbQueue(int32_t pollDelayInUs, enum KeyType type, int keysize,
                             IppCryptoSharedPtr ipp)
    : us_(std::chrono::microseconds(pollDelayInUs)),
      request_queue_(std::vector<CryptoMbContextSharedPtr>()), type_(type), key_size_(keysize),
      ipp_(ipp) {
  request_queue_.reserve(MULTIBUFF_BATCH);
}

void CryptoMbQueue::startTimer(Event::Dispatcher& dispatcher) {
  if (timer_ == nullptr) {
    timer_ = dispatcher.createTimer([this]() -> void { processRequests(); });
  }
  timer_->enableHRTimer(us_);
}

void CryptoMbQueue::disableTimer() { timer_->disableTimer(); }

void CryptoMbQueue::addAndProcessEightRequests(CryptoMbContextSharedPtr mb_ctx,
                                               Event::Dispatcher& dispatcher) {
  // Add the request to the processing queue.
  ASSERT(request_queue_.size() < MULTIBUFF_BATCH);
  request_queue_.push_back(mb_ctx);

  if (request_queue_.size() == 1) {
    // First request in the queue, start the queue timer.
    startTimer(dispatcher);
  } else if (request_queue_.size() == MULTIBUFF_BATCH) {
    // There are eight requests in the queue and we can process them.
    disableTimer();

    ENVOY_LOG(debug, "processing directly 8 requests");
    processRequests();
  }
}

void CryptoMbQueue::processRequests() {
  if (type_ == KeyType::rsa) {
    processRsaRequests();
  } else {
    processEcdsaRequests();
  }
  request_queue_.clear();
}

void CryptoMbQueue::processEcdsaRequests() {

  if (request_queue_.size() == 0) {
    return;
  }

  ASSERT(key_size_ == 256);

  const unsigned char* ecdsa_priv_from[MULTIBUFF_BATCH] = {0};
  const BIGNUM* pa_eph_skey[MULTIBUFF_BATCH] = {0};
  const BIGNUM* pa_reg_skey[MULTIBUFF_BATCH] = {0};

  /* Build arrays of pointers for call */
  for (unsigned req_num = 0; req_num < request_queue_.size(); req_num++) {
    const CryptoMbContextSharedPtr& mb_ctx = request_queue_[req_num];
    ecdsa_priv_from[req_num] = mb_ctx->in_buf_.get();
    pa_eph_skey[req_num] = &static_cast<CryptoMbEcdsaContext*>(mb_ctx.get())->k_;
    pa_reg_skey[req_num] = static_cast<CryptoMbEcdsaContext*>(mb_ctx.get())->s_;
  }

  ENVOY_LOG(debug, "Multibuffer ECDSA process {} requests", request_queue_.size());

  // Signature components. Size of r and s is the key size in bytes: 256/8=32
  int8u sign_r[MULTIBUFF_BATCH][32];
  int8u sign_s[MULTIBUFF_BATCH][32];
  int8u* pa_sign_r[MULTIBUFF_BATCH] = {sign_r[0], sign_r[1], sign_r[2], sign_r[3],
                                       sign_r[4], sign_r[5], sign_r[6], sign_r[7]};
  int8u* pa_sign_s[MULTIBUFF_BATCH] = {sign_s[0], sign_s[1], sign_s[2], sign_s[3],
                                       sign_s[4], sign_s[5], sign_s[6], sign_s[7]};

  unsigned int ecdsa_sts = ipp_->mbx_nistp256_ecdsa_sign_ssl_mb8(
      pa_sign_r, pa_sign_s, ecdsa_priv_from, pa_eph_skey, pa_reg_skey, nullptr);

  enum RequestStatus status[MULTIBUFF_BATCH] = {RequestStatus::retry};
  for (unsigned req_num = 0; req_num < request_queue_.size(); req_num++) {
    CryptoMbEcdsaContextSharedPtr mb_ctx =
        std::static_pointer_cast<CryptoMbEcdsaContext>(request_queue_[req_num]);
    enum RequestStatus ctx_status;
    if (MBX_GET_STS(ecdsa_sts, req_num) == MBX_STATUS_OK) {
      ENVOY_LOG(debug, "Multibuffer ECDSA priv crt req[{}] success", req_num);
      status[req_num] = RequestStatus::success;

      // Use previously known size of the r and s (32).
      BIGNUM* r = BN_bin2bn(pa_sign_r[req_num], 32, nullptr);
      BIGNUM* s = BN_bin2bn(pa_sign_s[req_num], 32, nullptr);

      if (r == nullptr || s == nullptr) {
        status[req_num] = RequestStatus::error;
      } else {
        ECDSA_SIG* sig = ECDSA_SIG_new();
        ECDSA_SIG_set0(sig, r, s);

        // Make sure that the signature fits into out_buf_.
        if (CryptoMbContext::MAX_SIGNATURE_SIZE < mb_ctx->ecdsa_sig_size_) {
          ENVOY_LOG(debug, "Multibuffer ECDSA priv crt req[{}] too long key size", req_num);
          status[req_num] = RequestStatus::error;
        } else {
          // BoringSSL uses CBB to marshaling the signature to out_buf_.
          CBB cbb;
          if (!CBB_init_fixed(&cbb, mb_ctx->out_buf_, mb_ctx->ecdsa_sig_size_) ||
              !ECDSA_SIG_marshal(&cbb, sig) || !CBB_finish(&cbb, nullptr, &mb_ctx->out_len_)) {
            ENVOY_LOG(debug, "Multibuffer ECDSA priv crt req[{}] failed to create signature",
                      req_num);
            status[req_num] = RequestStatus::error;
            CBB_cleanup(&cbb);
          }
        }
        ECDSA_SIG_free(sig);
      }
    } else {
      ENVOY_LOG(debug, "Multibuffer ECDSA priv crt req[{}] failure: {}", req_num,
                MBX_GET_STS(ecdsa_sts, req_num));
      status[req_num] = RequestStatus::error;
    }

    ctx_status = status[req_num];
    mb_ctx->scheduleCallback(ctx_status);
  }
}

void CryptoMbQueue::processRsaRequests() {

  if (request_queue_.size() == 0) {
    return;
  }

  const unsigned char* rsa_priv_from[MULTIBUFF_BATCH] = {0};
  unsigned char* rsa_priv_to[MULTIBUFF_BATCH] = {0};
  const BIGNUM* rsa_lenstra_e[MULTIBUFF_BATCH] = {0};
  const BIGNUM* rsa_lenstra_n[MULTIBUFF_BATCH] = {0};
  const BIGNUM* rsa_priv_p[MULTIBUFF_BATCH] = {0};
  const BIGNUM* rsa_priv_q[MULTIBUFF_BATCH] = {0};
  const BIGNUM* rsa_priv_dmp1[MULTIBUFF_BATCH] = {0};
  const BIGNUM* rsa_priv_dmq1[MULTIBUFF_BATCH] = {0};
  const BIGNUM* rsa_priv_iqmp[MULTIBUFF_BATCH] = {0};

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

  unsigned int rsa_sts =
      ipp_->mbx_rsa_private_crt_ssl_mb8(rsa_priv_from, rsa_priv_to, rsa_priv_p, rsa_priv_q,
                                        rsa_priv_dmp1, rsa_priv_dmq1, rsa_priv_iqmp, key_size_);

  enum RequestStatus status[MULTIBUFF_BATCH] = {RequestStatus::retry};

  for (unsigned req_num = 0; req_num < request_queue_.size(); req_num++) {
    CryptoMbRsaContextSharedPtr mb_ctx =
        std::static_pointer_cast<CryptoMbRsaContext>(request_queue_[req_num]);
    if (MBX_GET_STS(rsa_sts, req_num) == MBX_STATUS_OK) {
      ENVOY_LOG(debug, "Multibuffer RSA request {} success", req_num);
      status[req_num] = RequestStatus::success;
    } else {
      ENVOY_LOG(debug, "Multibuffer RSA request {} failure", req_num);
      status[req_num] = RequestStatus::error;
    }

    // Lenstra check (validate that we get the same result back).
    rsa_priv_from[req_num] = rsa_priv_to[req_num];
    rsa_priv_to[req_num] = mb_ctx->lenstra_to_;
    rsa_lenstra_e[req_num] = mb_ctx->e_;
    rsa_lenstra_n[req_num] = mb_ctx->n_;
  }

  rsa_sts = ipp_->mbx_rsa_public_ssl_mb8(rsa_priv_from, rsa_priv_to, rsa_lenstra_e, rsa_lenstra_n,
                                         key_size_);

  for (unsigned req_num = 0; req_num < request_queue_.size(); req_num++) {
    CryptoMbRsaContextSharedPtr mb_ctx =
        std::static_pointer_cast<CryptoMbRsaContext>(request_queue_[req_num]);
    enum RequestStatus ctx_status;
    if (MBX_GET_STS(rsa_sts, req_num) == MBX_STATUS_OK) {
      if (CRYPTO_memcmp(mb_ctx->in_buf_.get(), rsa_priv_to[req_num], mb_ctx->out_len_) != 0) {
        ENVOY_LOG(debug, "Multibuffer RSA request {} Lenstra check failure", req_num);
        status[req_num] = RequestStatus::error;
      }
      // else keep the previous status from the private key operation
    } else {
      ENVOY_LOG(debug, "Multibuffer RSA validation request {} failure", req_num);
      status[req_num] = RequestStatus::error;
    }

    ctx_status = status[req_num];
    mb_ctx->scheduleCallback(ctx_status);
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
    throw EnvoyException("Registering the CryptoMb provider twice for same context "
                         "is not yet supported.");
  }

  ASSERT(tls_->currentThreadRegistered(), "Current thread needs to be registered.");

  CryptoMbQueue& queue = tls_->get()->queue_;

  CryptoMbPrivateKeyConnection* ops =
      new CryptoMbPrivateKeyConnection(cb, dispatcher, bssl::UpRef(pkey_), queue);
  SSL_set_ex_data(ssl, CryptoMbPrivateKeyMethodProvider::connectionIndex(), ops);
}

void CryptoMbPrivateKeyConnection::addToQueue(CryptoMbContextSharedPtr mb_ctx) {
  mb_ctx_ = mb_ctx;
  queue_.addAndProcessEightRequests(mb_ctx_, dispatcher_);
}

bool CryptoMbPrivateKeyMethodProvider::checkFips() {
  RSA* rsa_private_key = EVP_PKEY_get0_RSA(pkey_.get());
  if (rsa_private_key == nullptr || !RSA_check_fips(rsa_private_key)) {
    return false;
  }
  return true;
}

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

CryptoMbPrivateKeyMethodProvider::CryptoMbPrivateKeyMethodProvider(
    const envoy::extensions::private_key_providers::cryptomb::v3::CryptoMbPrivateKeyMethodConfig&
        conf,
    Server::Configuration::TransportSocketFactoryContext& factory_context, IppCryptoSharedPtr ipp)
    : api_(factory_context.api()),
      tls_(ThreadLocal::TypedSlot<ThreadLocalData>::makeUnique(factory_context.threadLocal())) {

  if (!ipp->mbx_is_crypto_mb_applicable(0)) {
    throw EnvoyException("Multi-buffer CPU instructions not available.");
  }

  uint32_t poll_delay = conf.poll_delay();

  std::string private_key;

  if (conf.private_key_file() != "" && conf.inline_private_key() == "") {
    private_key = factory_context.api().fileSystem().fileReadToEnd(conf.private_key_file());
  } else if (conf.private_key_file() == "" && conf.inline_private_key() != "") {
    private_key = conf.inline_private_key();
  } else {
    throw EnvoyException("Exactly one of private_key or inline_private_key must be configured.");
  }

  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(const_cast<char*>(private_key.data()), private_key.size()));

  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    throw EnvoyException("Failed to read private key.");
  }

  method_ = std::make_shared<SSL_PRIVATE_KEY_METHOD>();

  enum KeyType key_type;
  int key_size;

  if (EVP_PKEY_id(pkey.get()) == EVP_PKEY_RSA) {
    ENVOY_LOG(debug, "CryptoMb key type: RSA");
    key_type = KeyType::rsa;

    method_->sign = rsaPrivateKeySign;
    method_->decrypt = rsaPrivateKeyDecrypt;
    method_->complete = privateKeyComplete;

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
      throw EnvoyException("Only RSA keys of 1024, 2048, 3072, and 4096 bits are supported.");
    }

    // If longer keys are ever supported, remember to change the signature buffer to be larger.
    ASSERT(key_size / 8 <= CryptoMbContext::MAX_SIGNATURE_SIZE);

    BIGNUM e_check;
    const BIGNUM *e, *n, *d;

    RSA_get0_key(rsa, &n, &e, &d);
    BN_init(&e_check);
    BN_add_word(&e_check, 65537);
    if (e == nullptr || BN_ucmp(e, &e_check) != 0) {
      throw EnvoyException("Only RSA keys with \"e\" parameter value 65537 are allowed, because "
                           "we can validate the signatures using multi-buffer instructions.");
    }
  } else if (EVP_PKEY_id(pkey.get()) == EVP_PKEY_EC) {
    ENVOY_LOG(debug, "CryptoMb key type: ECDSA");
    key_type = KeyType::ec;

    method_->sign = ecdsaPrivateKeySign;
    method_->decrypt = ecdsaPrivateKeyDecrypt;
    method_->complete = privateKeyComplete;

    EC_KEY* eckey = EVP_PKEY_get0_EC_KEY(pkey.get());
    const EC_GROUP* ecdsa_group = EC_KEY_get0_group(eckey);
    const BIGNUM* order = EC_GROUP_get0_order(ecdsa_group);
    if (ecdsa_group == nullptr || EC_GROUP_get_curve_name(ecdsa_group) != NID_X9_62_prime256v1) {
      throw EnvoyException("Only P-256 ECDSA keys are supported.");
    }
    if (BN_num_bits(order) < 160) {
      throw EnvoyException("Too few significant bits.");
    }
    key_size = EC_GROUP_get_degree(ecdsa_group);
    ASSERT(key_size == 256);
  } else {
    throw EnvoyException("Not supported key type, only EC and RSA are supported.");
  }

  pkey_ = std::move(pkey);

  // Create a single queue for every worker thread to avoid locking.
  tls_->set([poll_delay, key_type, key_size, ipp](Event::Dispatcher& d) {
    ENVOY_LOG(debug, "Created CryptoMb Queue for thread {}", d.name());
    return std::make_shared<ThreadLocalData>(poll_delay, key_type, key_size, ipp);
  });
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

} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
