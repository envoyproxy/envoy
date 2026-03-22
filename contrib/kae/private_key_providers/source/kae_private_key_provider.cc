#include "contrib/kae/private_key_providers/source/kae_private_key_provider.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>

#include "envoy/registry/registry.h"
#include "envoy/server/transport_socket_config.h"

#include "source/common/config/datasource.h"

#include "absl/cleanup/cleanup.h"
#include "contrib/kae/private_key_providers/source/kae.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {
namespace Kae {

SINGLETON_MANAGER_REGISTRATION(kae_manager);

// KaePrivateKeyConnection
KaePrivateKeyConnection::KaePrivateKeyConnection(Ssl::PrivateKeyConnectionCallbacks& cb,
                                                 Event::Dispatcher& dispatcher, KaeHandle& handle,
                                                 bssl::UniquePtr<EVP_PKEY> pkey)
    : cb_(cb), dispatcher_(dispatcher), handle_(handle), pkey_(std::move(pkey)) {}

void KaePrivateKeyConnection::registerCallback(KaeContext* ctx) {

  // Get the receiving end of the notification pipe. The other end is written to by the polling
  // thread.
  int fd = ctx->getFd();

  ssl_async_event_ = dispatcher_.createFileEvent(
      fd,
      [this, ctx, fd](uint32_t) {
        int status = WD_STATUS_FAILED;
        {
          Thread::LockGuard data_lock(ctx->data_lock_);
          int bytes = read(fd, &status, sizeof(status));
          if (bytes != sizeof(status)) {
            status = WD_STATUS_FAILED;
          }
          if (status == WD_STATUS_BUSY) {
            // At this point we are no longer allowed to have the status as "busy", because the
            // upper levels consider the operation complete.
            status = WD_STATUS_FAILED;
          }
          ctx->setOpStatus(status);
        }
        this->cb_.onPrivateKeyMethodComplete();
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
}

void KaePrivateKeyConnection::unregisterCallback() { ssl_async_event_ = nullptr; }

namespace {
ssl_private_key_result_t privateKeySignInternal(SSL* ssl, KaePrivateKeyConnection* ops, uint8_t*,
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
  int padding = RSA_NO_PADDING;

  absl::Cleanup msg_cleanup = [&] {
    if (prefix_allocated) {
      OPENSSL_free(msg);
    }
  };

  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  KaeHandle& kae_handle = ops->getHandle();
  EVP_PKEY* rsa_pkey = ops->getPrivateKey();

  if (rsa_pkey == nullptr) {
    return ssl_private_key_failure;
  }
  if (EVP_PKEY_id(rsa_pkey) != SSL_get_signature_algorithm_key_type(signature_algorithm)) {
    return ssl_private_key_failure;
  }

  rsa = EVP_PKEY_get0_RSA(rsa_pkey);
  if (rsa == nullptr) {
    return ssl_private_key_failure;
  }
  md = SSL_get_signature_algorithm_digest(signature_algorithm);
  if (md == nullptr) {
    return ssl_private_key_failure;
  }

  // Create KAE context which will be used for this particular signing/decryption.
  auto kae_ctx = std::make_unique<KaeContext>(kae_handle);
  if (kae_ctx.get() == nullptr || !kae_ctx->init()) {
    return ssl_private_key_failure;
  }

  // The fd will become readable when the KAE operation has been completed.
  ops->registerCallback(kae_ctx.get());

  if (ssl) {
    // Associate the SSL instance with the KAE Context. The SSL instance might be nullptr if this is
    // called from a test context.
    if (!SSL_set_ex_data(ssl, KaeManager::contextIndex(), kae_ctx.get())) {
      return ssl_private_key_failure;
    }
  }

  // Calculate the digest for signing.
  if (!EVP_DigestInit_ex(ctx.get(), md, nullptr) || !EVP_DigestUpdate(ctx.get(), in, in_len) ||
      !EVP_DigestFinal_ex(ctx.get(), hash, &hash_len)) {
    return ssl_private_key_failure;
  }

  // Add RSA padding to the the hash. Supported types are PSS and PKCS1.
  if (SSL_is_signature_algorithm_rsa_pss(signature_algorithm)) {
    msg_len = RSA_size(rsa);
    msg = static_cast<uint8_t*>(OPENSSL_malloc(msg_len));
    if (!msg) {
      return ssl_private_key_failure;
    }
    prefix_allocated = 1;
    if (!RSA_padding_add_PKCS1_PSS_mgf1(rsa, msg, hash, md, nullptr, -1)) {
      return ssl_private_key_failure;
    }
    padding = RSA_NO_PADDING;
  } else {
    if (!RSA_add_pkcs1_prefix(&msg, &msg_len, &prefix_allocated, EVP_MD_type(md), hash, hash_len)) {
      return ssl_private_key_failure;
    }
    padding = RSA_PKCS1_PADDING;
  }

  // Start KAE decryption (signing) operation.
  if (!kae_ctx->decrypt(msg_len, msg, rsa, padding)) {
    return ssl_private_key_failure;
  }

  // kae_ctx will be deleted in complete function
  kae_ctx.release();

  return ssl_private_key_retry;
}

ssl_private_key_result_t privateKeySign(SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out,
                                        uint16_t signature_algorithm, const uint8_t* in,
                                        size_t in_len) {
  return ssl == nullptr
             ? ssl_private_key_failure
             : privateKeySignInternal(ssl,
                                      static_cast<KaePrivateKeyConnection*>(
                                          SSL_get_ex_data(ssl, KaeManager::connectionIndex())),
                                      out, out_len, max_out, signature_algorithm, in, in_len);
}

ssl_private_key_result_t privateKeyDecryptInternal(SSL* ssl, KaePrivateKeyConnection* ops, uint8_t*,
                                                   size_t*, size_t, const uint8_t* in,
                                                   size_t in_len) {
  RSA* rsa;

  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  KaeHandle& kae_handle = ops->getHandle();
  EVP_PKEY* rsa_pkey = ops->getPrivateKey();

  // Check if the SSL instance has correct data attached to it.
  if (!rsa_pkey) {
    return ssl_private_key_failure;
  }

  rsa = EVP_PKEY_get0_RSA(rsa_pkey);
  if (rsa == nullptr) {
    return ssl_private_key_failure;
  }

  // Create KAE context which will be used for this particular signing/decryption.
  auto kae_ctx = std::make_unique<KaeContext>(kae_handle);
  if (kae_ctx.get() == nullptr || !kae_ctx->init()) {
    return ssl_private_key_failure;
  }

  // The fd will become readable when the KAE operation has been completed.
  ops->registerCallback(kae_ctx.get());

  // Associate the SSL instance with the KAE Context.
  if (ssl) {
    if (!SSL_set_ex_data(ssl, KaeManager::contextIndex(), kae_ctx.get())) {
      return ssl_private_key_failure;
    }
  }

  // Start KAE decryption (signing) operation.
  if (!kae_ctx->decrypt(in_len, in, rsa, RSA_NO_PADDING)) {
    return ssl_private_key_failure;
  }

  kae_ctx.release();

  return ssl_private_key_retry;
}

ssl_private_key_result_t privateKeyDecrypt(SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out,
                                           const uint8_t* in, size_t in_len) {
  return ssl == nullptr
             ? ssl_private_key_failure
             : privateKeyDecryptInternal(ssl,
                                         static_cast<KaePrivateKeyConnection*>(
                                             SSL_get_ex_data(ssl, KaeManager::connectionIndex())),
                                         out, out_len, max_out, in, in_len);
}

ssl_private_key_result_t privateKeyCompleteInternal(SSL* ssl, KaePrivateKeyConnection* ops,
                                                    KaeContext* kae_ctx, uint8_t* out,
                                                    size_t* out_len, size_t max_out) {

  if (kae_ctx == nullptr) {
    return ssl_private_key_failure;
  }

  // Check if the KAE operation is ready yet. This can happen if someone calls
  // the top-level SSL function too early. The op status is only set from this thread.
  if (kae_ctx->getOpStatus() == WD_STATUS_BUSY) {
    return ssl_private_key_retry;
  }

  // If this point is reached, the KAE processing must be complete. We are allowed to delete the
  // KAE_ctx now without fear of the polling thread trying to use it.

  if (ops == nullptr) {
    return ssl_private_key_failure;
  }

  // Unregister the callback to prevent it from being called again when the pipe is closed.
  ops->unregisterCallback();

  // See if the operation failed.
  if (kae_ctx->getOpStatus() != WD_SUCCESS) {
    delete kae_ctx;
    return ssl_private_key_failure;
  }

  *out_len = kae_ctx->getDecryptedDataLength();

  if (*out_len > max_out) {
    delete kae_ctx;
    return ssl_private_key_failure;
  }

  memcpy(out, kae_ctx->getDecryptedData(), *out_len); // NOLINT(safe-memcpy)

  if (ssl) {
    SSL_set_ex_data(ssl, KaeManager::contextIndex(), nullptr);
  }

  delete kae_ctx;
  return ssl_private_key_success;
}

ssl_private_key_result_t privateKeyComplete(SSL* ssl, uint8_t* out, size_t* out_len,
                                            size_t max_out) {

  if (ssl == nullptr) {
    return ssl_private_key_failure;
  }
  KaeContext* kae_ctx = static_cast<KaeContext*>(SSL_get_ex_data(ssl, KaeManager::contextIndex()));
  KaePrivateKeyConnection* ops =
      static_cast<KaePrivateKeyConnection*>(SSL_get_ex_data(ssl, KaeManager::connectionIndex()));

  return privateKeyCompleteInternal(ssl, ops, kae_ctx, out, out_len, max_out);
}

} // namespace

// External linking, meant for testing without SSL context.
ssl_private_key_result_t privateKeySignForTest(KaePrivateKeyConnection* ops, uint8_t* out,
                                               size_t* out_len, size_t max_out,
                                               uint16_t signature_algorithm, const uint8_t* in,
                                               size_t in_len) {
  return privateKeySignInternal(nullptr, ops, out, out_len, max_out, signature_algorithm, in,
                                in_len);
}
ssl_private_key_result_t privateKeyDecryptForTest(KaePrivateKeyConnection* ops, uint8_t* out,
                                                  size_t* out_len, size_t max_out,
                                                  const uint8_t* in, size_t in_len) {
  return privateKeyDecryptInternal(nullptr, ops, out, out_len, max_out, in, in_len);
}
ssl_private_key_result_t privateKeyCompleteForTest(KaePrivateKeyConnection* ops,
                                                   KaeContext* kae_ctx, uint8_t* out,
                                                   size_t* out_len, size_t max_out) {
  return privateKeyCompleteInternal(nullptr, ops, kae_ctx, out, out_len, max_out);
}

Ssl::BoringSslPrivateKeyMethodSharedPtr
KaePrivateKeyMethodProvider::getBoringSslPrivateKeyMethod() {
  return method_;
}

bool KaePrivateKeyMethodProvider::checkFips() { return false; }
bool KaePrivateKeyMethodProvider::isAvailable() { return initialized_; }

void KaePrivateKeyMethodProvider::registerPrivateKeyMethod(SSL* ssl,
                                                           Ssl::PrivateKeyConnectionCallbacks& cb,
                                                           Event::Dispatcher& dispatcher) {
  if (section_ == nullptr || !section_->isInitialized()) {
    throw EnvoyException("KAE isn't properly initialized.");
  }

  if (SSL_get_ex_data(ssl, KaeManager::connectionIndex()) != nullptr) {
    throw EnvoyException(
        "Registering the KAE provider twice for same context is not yet supported.");
  }

  KaeHandle& handle = section_->getNextHandle();

  KaePrivateKeyConnection* ops =
      new KaePrivateKeyConnection(cb, dispatcher, handle, bssl::UpRef(pkey_));
  SSL_set_ex_data(ssl, KaeManager::connectionIndex(), ops);
}

void KaePrivateKeyMethodProvider::unregisterPrivateKeyMethod(SSL* ssl) {
  KaePrivateKeyConnection* ops =
      static_cast<KaePrivateKeyConnection*>(SSL_get_ex_data(ssl, KaeManager::connectionIndex()));
  SSL_set_ex_data(ssl, KaeManager::connectionIndex(), nullptr);
  delete ops;
}

KaePrivateKeyMethodProvider::KaePrivateKeyMethodProvider(
    const envoy::extensions::private_key_providers::kae::v3alpha::KaePrivateKeyMethodConfig& conf,
    Server::Configuration::TransportSocketFactoryContext& factory_context,
    LibUadkCryptoSharedPtr libuadk)
    : api_(factory_context.serverFactoryContext().api()), libuadk_(libuadk) {
  manager_ = factory_context.serverFactoryContext().singletonManager().getTyped<KaeManager>(
      SINGLETON_MANAGER_REGISTERED_NAME(kae_manager),
      [libuadk] { return std::make_shared<KaeManager>(libuadk); });
  ASSERT(manager_);

  if (!manager_->checkKaeDevice()) {
    return;
  }

  std::chrono::milliseconds poll_delay =
      std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(conf, poll_delay, 5));

  std::string private_key =
      THROW_OR_RETURN_VALUE(Config::DataSource::read(conf.private_key(), false, api_), std::string);

  const uint32_t max_instances = PROTOBUF_GET_WRAPPED_OR_DEFAULT(conf, max_instances, 16);

  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(const_cast<char*>(private_key.data()), private_key.size()));

  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    throw EnvoyException("Failed to read private key.");
  }

  if (EVP_PKEY_id(pkey.get()) != EVP_PKEY_RSA) {
    ENVOY_LOG(warn, "Only RSA keys are supported.");
    return;
  }
  pkey_ = std::move(pkey);

  section_ = std::make_shared<KaeSection>(libuadk);
  if (!section_->startSection(api_, poll_delay, max_instances)) {
    ENVOY_LOG(warn, "Failed to start KAE.");
    return;
  }

  method_ = std::make_shared<SSL_PRIVATE_KEY_METHOD>();
  method_->sign = privateKeySign;
  method_->decrypt = privateKeyDecrypt;
  method_->complete = privateKeyComplete;

  initialized_ = true;
  ENVOY_LOG(info, "initialized KAE private key provider");
}

} // namespace Kae
} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
