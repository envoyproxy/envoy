#include "contrib/qat/private_key_providers/source/qat_private_key_provider.h"

#include <memory>

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/datasource.h"

#include "contrib/qat/private_key_providers/source/qat.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Qat {

SINGLETON_MANAGER_REGISTRATION(qat_manager);

void QatPrivateKeyConnection::registerCallback(QatContext* ctx) {

  // Get the receiving end of the notification pipe. The other end is written to by the polling
  // thread.
  int fd = ctx->getFd();

  ssl_async_event_ = dispatcher_.createFileEvent(
      fd,
      [this, ctx, fd](uint32_t) {
        CpaStatus status = CPA_STATUS_FAIL;
        {
          Thread::LockGuard data_lock(ctx->data_lock_);
          int bytes = read(fd, &status, sizeof(status));
          if (bytes != sizeof(status)) {
            status = CPA_STATUS_FAIL;
          }
          if (status == CPA_STATUS_RETRY) {
            // At this point we are no longer allowed to have the status as "retry", because the
            // upper levels consider the operation complete.
            status = CPA_STATUS_FAIL;
          }
          ctx->setOpStatus(status);
        }
        this->cb_.onPrivateKeyMethodComplete();
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
}

void QatPrivateKeyConnection::unregisterCallback() { ssl_async_event_ = nullptr; }

namespace {

ssl_private_key_result_t privateKeySignInternal(SSL* ssl, QatPrivateKeyConnection* ops, uint8_t*,
                                                size_t*, size_t, uint16_t signature_algorithm,
                                                const uint8_t* in, size_t in_len) {
  RSA* rsa;
  const EVP_MD* md;
  bssl::ScopedEVP_MD_CTX ctx;
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;
  uint8_t* msg;
  size_t msg_len;
  int prefix_allocated = 0;
  QatContext* qat_ctx = nullptr;
  int padding = RSA_NO_PADDING;

  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  QatHandle& qat_handle = ops->getHandle();

  EVP_PKEY* rsa_pkey = ops->getPrivateKey();

  // Check if the SSL instance has correct data attached to it.
  if (rsa_pkey == nullptr) {
    goto error;
  }

  if (EVP_PKEY_id(rsa_pkey) != SSL_get_signature_algorithm_key_type(signature_algorithm)) {
    goto error;
  }

  rsa = EVP_PKEY_get0_RSA(rsa_pkey);
  if (rsa == nullptr) {
    goto error;
  }

  md = SSL_get_signature_algorithm_digest(signature_algorithm);
  if (md == nullptr) {
    goto error;
  }

  // Create QAT context which will be used for this particular signing/decryption.
  qat_ctx = new QatContext(qat_handle);
  if (qat_ctx == nullptr || !qat_ctx->init()) {
    goto error;
  }

  // The fd will become readable when the QAT operation has been completed.
  ops->registerCallback(qat_ctx);

  if (ssl) {
    // Associate the SSL instance with the QAT Context. The SSL instance might be nullptr if this is
    // called from a test context.
    if (!SSL_set_ex_data(ssl, QatManager::contextIndex(), qat_ctx)) {
      goto error;
    }
  }

  // Calculate the digest for signing.
  if (!EVP_DigestInit_ex(ctx.get(), md, nullptr) || !EVP_DigestUpdate(ctx.get(), in, in_len) ||
      !EVP_DigestFinal_ex(ctx.get(), hash, &hash_len)) {
    goto error;
  }

  // Add RSA padding to the the hash. Supported types are PSS and PKCS1.
  if (SSL_is_signature_algorithm_rsa_pss(signature_algorithm)) {
    msg_len = RSA_size(rsa);
    msg = static_cast<uint8_t*>(OPENSSL_malloc(msg_len));
    if (!msg) {
      goto error;
    }
    prefix_allocated = 1;
    if (!RSA_padding_add_PKCS1_PSS_mgf1(rsa, msg, hash, md, nullptr, -1)) {
      goto error;
    }
    padding = RSA_NO_PADDING;
  } else {
    if (!RSA_add_pkcs1_prefix(&msg, &msg_len, &prefix_allocated, EVP_MD_type(md), hash, hash_len)) {
      goto error;
    }
    padding = RSA_PKCS1_PADDING;
  }

  // Start QAT decryption (signing) operation.
  if (!qat_ctx->decrypt(msg_len, msg, rsa, padding)) {
    goto error;
  }

  if (prefix_allocated) {
    OPENSSL_free(msg);
  }

  return ssl_private_key_retry;

error:
  if (prefix_allocated) {
    OPENSSL_free(msg);
  }
  delete qat_ctx;
  return ssl_private_key_failure;
}

ssl_private_key_result_t privateKeySign(SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out,
                                        uint16_t signature_algorithm, const uint8_t* in,
                                        size_t in_len) {
  return ssl == nullptr
             ? ssl_private_key_failure
             : privateKeySignInternal(ssl,
                                      static_cast<QatPrivateKeyConnection*>(
                                          SSL_get_ex_data(ssl, QatManager::connectionIndex())),
                                      out, out_len, max_out, signature_algorithm, in, in_len);
}

ssl_private_key_result_t privateKeyDecryptInternal(SSL* ssl, QatPrivateKeyConnection* ops, uint8_t*,
                                                   size_t*, size_t, const uint8_t* in,
                                                   size_t in_len) {
  RSA* rsa;
  QatContext* qat_ctx = nullptr;

  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  QatHandle& qat_handle = ops->getHandle();
  EVP_PKEY* rsa_pkey = ops->getPrivateKey();

  // Check if the SSL instance has correct data attached to it.
  if (!rsa_pkey) {
    goto error;
  }

  rsa = EVP_PKEY_get0_RSA(rsa_pkey);
  if (rsa == nullptr) {
    goto error;
  }

  // Create QAT context which will be used for this particular signing/decryption.
  qat_ctx = new QatContext(qat_handle);
  if (qat_ctx == nullptr || !qat_ctx->init()) {
    goto error;
  }

  // The fd will become readable when the QAT operation has been completed.
  ops->registerCallback(qat_ctx);

  // Associate the SSL instance with the QAT Context.
  if (ssl) {
    if (!SSL_set_ex_data(ssl, QatManager::contextIndex(), qat_ctx)) {
      goto error;
    }
  }

  // Start QAT decryption (signing) operation.
  if (!qat_ctx->decrypt(in_len, in, rsa, RSA_NO_PADDING)) {
    goto error;
  }

  return ssl_private_key_retry;

error:
  delete qat_ctx;
  return ssl_private_key_failure;
}

ssl_private_key_result_t privateKeyDecrypt(SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out,
                                           const uint8_t* in, size_t in_len) {
  return ssl == nullptr
             ? ssl_private_key_failure
             : privateKeyDecryptInternal(ssl,
                                         static_cast<QatPrivateKeyConnection*>(
                                             SSL_get_ex_data(ssl, QatManager::connectionIndex())),
                                         out, out_len, max_out, in, in_len);
}

ssl_private_key_result_t privateKeyCompleteInternal(SSL* ssl, QatPrivateKeyConnection* ops,
                                                    QatContext* qat_ctx, uint8_t* out,
                                                    size_t* out_len, size_t max_out) {

  if (qat_ctx == nullptr) {
    return ssl_private_key_failure;
  }

  // Check if the QAT operation is ready yet. This can happen if someone calls
  // the top-level SSL function too early. The op status is only set from this thread.
  if (qat_ctx->getOpStatus() == CPA_STATUS_RETRY) {
    return ssl_private_key_retry;
  }

  // If this point is reached, the QAT processing must be complete. We are allowed to delete the
  // qat_ctx now without fear of the polling thread trying to use it.

  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  // Unregister the callback to prevent it from being called again when the pipe is closed.
  ops->unregisterCallback();

  // See if the operation failed.
  if (qat_ctx->getOpStatus() != CPA_STATUS_SUCCESS) {
    delete qat_ctx;
    return ssl_private_key_failure;
  }

  *out_len = qat_ctx->getDecryptedDataLength();

  if (*out_len > max_out) {
    delete qat_ctx;
    return ssl_private_key_failure;
  }

  memcpy(out, qat_ctx->getDecryptedData(), *out_len); // NOLINT(safe-memcpy)

  if (ssl) {
    SSL_set_ex_data(ssl, QatManager::contextIndex(), nullptr);
  }

  delete qat_ctx;
  return ssl_private_key_success;
}

ssl_private_key_result_t privateKeyComplete(SSL* ssl, uint8_t* out, size_t* out_len,
                                            size_t max_out) {

  if (ssl == nullptr) {
    return ssl_private_key_failure;
  }
  QatContext* qat_ctx = static_cast<QatContext*>(SSL_get_ex_data(ssl, QatManager::contextIndex()));
  QatPrivateKeyConnection* ops =
      static_cast<QatPrivateKeyConnection*>(SSL_get_ex_data(ssl, QatManager::connectionIndex()));

  return privateKeyCompleteInternal(ssl, ops, qat_ctx, out, out_len, max_out);
}

} // namespace

// External linking, meant for testing without SSL context.
ssl_private_key_result_t privateKeySignForTest(QatPrivateKeyConnection* ops, uint8_t* out,
                                               size_t* out_len, size_t max_out,
                                               uint16_t signature_algorithm, const uint8_t* in,
                                               size_t in_len) {
  return privateKeySignInternal(nullptr, ops, out, out_len, max_out, signature_algorithm, in,
                                in_len);
}
ssl_private_key_result_t privateKeyDecryptForTest(QatPrivateKeyConnection* ops, uint8_t* out,
                                                  size_t* out_len, size_t max_out,
                                                  const uint8_t* in, size_t in_len) {
  return privateKeyDecryptInternal(nullptr, ops, out, out_len, max_out, in, in_len);
}
ssl_private_key_result_t privateKeyCompleteForTest(QatPrivateKeyConnection* ops,
                                                   QatContext* qat_ctx, uint8_t* out,
                                                   size_t* out_len, size_t max_out) {
  return privateKeyCompleteInternal(nullptr, ops, qat_ctx, out, out_len, max_out);
}

Ssl::BoringSslPrivateKeyMethodSharedPtr
QatPrivateKeyMethodProvider::getBoringSslPrivateKeyMethod() {
  return method_;
}

bool QatPrivateKeyMethodProvider::checkFips() { return false; }
bool QatPrivateKeyMethodProvider::isAvailable() { return initialized_; }

QatPrivateKeyConnection::QatPrivateKeyConnection(Ssl::PrivateKeyConnectionCallbacks& cb,
                                                 Event::Dispatcher& dispatcher, QatHandle& handle,
                                                 bssl::UniquePtr<EVP_PKEY> pkey)
    : cb_(cb), dispatcher_(dispatcher), handle_(handle), pkey_(std::move(pkey)) {}

void QatPrivateKeyMethodProvider::registerPrivateKeyMethod(SSL* ssl,
                                                           Ssl::PrivateKeyConnectionCallbacks& cb,
                                                           Event::Dispatcher& dispatcher) {

  if (section_ == nullptr || !section_->isInitialized()) {
    throw EnvoyException("QAT isn't properly initialized.");
  }

  if (SSL_get_ex_data(ssl, QatManager::connectionIndex()) != nullptr) {
    throw EnvoyException(
        "Registering the QAT provider twice for same context is not yet supported.");
  }

  QatHandle& handle = section_->getNextHandle();

  QatPrivateKeyConnection* ops =
      new QatPrivateKeyConnection(cb, dispatcher, handle, bssl::UpRef(pkey_));
  SSL_set_ex_data(ssl, QatManager::connectionIndex(), ops);
}

void QatPrivateKeyMethodProvider::unregisterPrivateKeyMethod(SSL* ssl) {
  QatPrivateKeyConnection* ops =
      static_cast<QatPrivateKeyConnection*>(SSL_get_ex_data(ssl, QatManager::connectionIndex()));
  SSL_set_ex_data(ssl, QatManager::connectionIndex(), nullptr);
  delete ops;
}

QatPrivateKeyMethodProvider::QatPrivateKeyMethodProvider(
    const envoy::extensions::private_key_providers::qat::v3alpha::QatPrivateKeyMethodConfig& conf,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    LibQatCryptoSharedPtr libqat)
    : api_(factory_context.serverFactoryContext().api()), libqat_(libqat) {

  manager_ = factory_context.serverFactoryContext().singletonManager().getTyped<QatManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(qat_manager),
      [libqat] { return std::make_shared<QatManager>(libqat); });

  ASSERT(manager_);

  if (!manager_->checkQatDevice()) {
    return;
  }

  std::chrono::milliseconds poll_delay =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(conf, poll_delay, 5));

  std::string private_key =
      THROW_OR_RETURN_VALUE(Config::DataSource::read(conf.private_key(), false, api_), std::string);

  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(const_cast<char*>(private_key.data()), private_key.size()));

  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    throw EnvoyException("Failed to read private key.");
  }

  if (EVP_PKEY_id(pkey.get()) != EVP_PKEY_RSA) {
    // TODO(ipuustin): add support also to ECDSA keys.
    ENVOY_LOG(warn, "Only RSA keys are supported.");
    return;
  }
  pkey_ = std::move(pkey);

  section_ = std::make_shared<QatSection>(libqat);
  if (!section_->startSection(api_, poll_delay)) {
    ENVOY_LOG(warn, "Failed to start QAT.");
    return;
  }

  method_ = std::make_shared<SSL_PRIVATE_KEY_METHOD>();
  method_->sign = privateKeySign;
  method_->decrypt = privateKeyDecrypt;
  method_->complete = privateKeyComplete;

  initialized_ = true;
  ENVOY_LOG(info, "initialized QAT private key provider");
}

} // namespace Qat
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
