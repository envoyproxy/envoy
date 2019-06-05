#include "test/extensions/transport_sockets/tls/rsa_private_key_method_provider.h"

#include <memory>

#include "envoy/api/api.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {

int RsaPrivateKeyMethodProvider::ssl_rsa_connection_index = -1;

void RsaPrivateKeyConnection::delayed_op() {
  const std::chrono::milliseconds timeout_0ms{0};

  timer_ = dispatcher_.createTimer([this]() -> void {
    finished_ = true;
    this->cb_.onPrivateKeyMethodComplete();
    return;
  });
  timer_->enableTimer(timeout_0ms);
}

static ssl_private_key_result_t privateKeySign(SSL* ssl, uint8_t* out, size_t* out_len,
                                               size_t max_out, uint16_t signature_algorithm,
                                               const uint8_t* in, size_t in_len) {
  RSA* rsa;
  bssl::ScopedEVP_MD_CTX ctx;
  const EVP_MD* md;
  RsaPrivateKeyConnection* ops = static_cast<RsaPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, RsaPrivateKeyMethodProvider::ssl_rsa_connection_index));
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;
  uint8_t* msg;
  size_t msg_len;
  int prefix_allocated = 0;
  int padding = RSA_NO_PADDING;

  if (!ops) {
    return ssl_private_key_failure;
  }

  if (ops->test_options_.method_error_) {
    // Have an artificial test failure.
    return ssl_private_key_failure;
  }

  if (!ops->test_options_.sign_expected_) {
    // TODO(ipuustin): throw exception, because a failure can be an expected result in some tests?
    return ssl_private_key_failure;
  }

  rsa = ops->getPrivateKey();
  if (rsa == nullptr) {
    return ssl_private_key_failure;
  }

  md = SSL_get_signature_algorithm_digest(signature_algorithm);
  if (!md) {
    return ssl_private_key_failure;
  }

  // Calculate the digest for signing.
  if (!EVP_DigestInit_ex(ctx.get(), md, nullptr) || !EVP_DigestUpdate(ctx.get(), in, in_len) ||
      !EVP_DigestFinal_ex(ctx.get(), hash, &hash_len)) {
    return ssl_private_key_failure;
  }

  // Add RSA padding to the the hash.
  if (SSL_is_signature_algorithm_rsa_pss(signature_algorithm)) {
    msg_len = RSA_size(rsa);
    msg = static_cast<uint8_t*>(OPENSSL_malloc(msg_len));
    if (!msg) {
      return ssl_private_key_failure;
    }
    prefix_allocated = 1;
    if (!RSA_padding_add_PKCS1_PSS_mgf1(rsa, msg, hash, md, nullptr, -1)) {
      OPENSSL_free(msg);
      return ssl_private_key_failure;
    }
    padding = RSA_NO_PADDING;
  } else {
    if (!RSA_add_pkcs1_prefix(&msg, &msg_len, &prefix_allocated, EVP_MD_type(md), hash, hash_len)) {
      return ssl_private_key_failure;
    }
    padding = RSA_PKCS1_PADDING;
  }

  if (!RSA_sign_raw(rsa, out_len, out, max_out, msg, msg_len, padding)) {
    if (prefix_allocated) {
      OPENSSL_free(msg);
    }
    return ssl_private_key_failure;
  }

  if (prefix_allocated) {
    OPENSSL_free(msg);
  }

  if (ops->test_options_.crypto_error_) {
    // Flip the bits in the first byte to cause the handshake to fail.
    out[0] ^= out[0];
  }

  if (ops->test_options_.sync_mode_) {
    // Return immediately with the results.
    return ssl_private_key_success;
  }

  ops->output_.assign(out, out + *out_len);

  // Tell SSL socket that the operation is ready to be called again.
  ops->delayed_op();

  return ssl_private_key_retry;
}

static ssl_private_key_result_t privateKeyDecrypt(SSL* ssl, uint8_t* out, size_t* out_len,
                                                  size_t max_out, const uint8_t* in,
                                                  size_t in_len) {
  RSA* rsa;
  RsaPrivateKeyConnection* ops = static_cast<RsaPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, RsaPrivateKeyMethodProvider::ssl_rsa_connection_index));

  if (!ops) {
    return ssl_private_key_failure;
  }

  if (ops->test_options_.method_error_) {
    // Have an artificial test failure.
    return ssl_private_key_failure;
  }

  if (!ops->test_options_.decrypt_expected_) {
    // TODO(ipuustin): throw exception, because a failure can be an expected result in some tests?
    return ssl_private_key_failure;
  }

  rsa = ops->getPrivateKey();
  if (rsa == nullptr) {
    return ssl_private_key_failure;
  }

  if (!RSA_decrypt(rsa, out_len, out, max_out, in, in_len, RSA_NO_PADDING)) {
    return ssl_private_key_failure;
  }

  if (ops->test_options_.sync_mode_) {
    // Return immediately with the results.
    return ssl_private_key_success;
  }

  ops->output_.assign(out, out + *out_len);

  ops->delayed_op();

  return ssl_private_key_retry;
}

static ssl_private_key_result_t privateKeyComplete(SSL* ssl, uint8_t* out, size_t* out_len,
                                                   size_t max_out) {
  RsaPrivateKeyConnection* ops = static_cast<RsaPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, RsaPrivateKeyMethodProvider::ssl_rsa_connection_index));

  if (!ops->finished_) {
    // The operation didn't finish yet, retry.
    return ssl_private_key_retry;
  }

  if (ops->test_options_.async_method_error_) {
    return ssl_private_key_failure;
  }

  if (ops->output_.size() > max_out) {
    return ssl_private_key_failure;
  }

  std::copy(ops->output_.begin(), ops->output_.end(), out);
  *out_len = ops->output_.size();

  return ssl_private_key_success;
}

Ssl::BoringSslPrivateKeyMethodSharedPtr
RsaPrivateKeyMethodProvider::getBoringSslPrivateKeyMethod() {
  return method_;
}

bool RsaPrivateKeyMethodProvider::checkFips() {
  RSA* rsa_private_key = EVP_PKEY_get0_RSA(pkey_.get());
  if (rsa_private_key == nullptr || !RSA_check_fips(rsa_private_key)) {
    return false;
  }
  return true;
}

RsaPrivateKeyConnection::RsaPrivateKeyConnection(SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb,
                                                 Event::Dispatcher& dispatcher,
                                                 bssl::UniquePtr<EVP_PKEY> pkey,
                                                 RsaPrivateKeyConnectionTestOptions& test_options)
    : test_options_(test_options), cb_(cb), dispatcher_(dispatcher), pkey_(std::move(pkey)) {
  SSL_set_ex_data(ssl, RsaPrivateKeyMethodProvider::ssl_rsa_connection_index, this);
}

void RsaPrivateKeyMethodProvider::registerPrivateKeyMethod(SSL* ssl,
                                                           Ssl::PrivateKeyConnectionCallbacks& cb,
                                                           Event::Dispatcher& dispatcher) {
  Thread::LockGuard map_lock(map_lock_);
  connections_.emplace(
      ssl, new RsaPrivateKeyConnection(ssl, cb, dispatcher, bssl::UpRef(pkey_), test_options_));
}

void RsaPrivateKeyMethodProvider::unregisterPrivateKeyMethod(SSL* ssl) {
  Thread::LockGuard map_lock(map_lock_);
  connections_.erase(ssl);
}

RsaPrivateKeyMethodProvider::RsaPrivateKeyMethodProvider(
    const ProtobufWkt::Struct& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context) {

  std::string private_key_path;

  if (RsaPrivateKeyMethodProvider::ssl_rsa_connection_index == -1) {
    RsaPrivateKeyMethodProvider::ssl_rsa_connection_index =
        SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  }

  for (auto& value_it : config.fields()) {
    auto& value = value_it.second;
    if (value_it.first == "private_key_file" &&
        value.kind_case() == ProtobufWkt::Value::kStringValue) {
      private_key_path = value.string_value();
    }
    if (value_it.first == "sync_mode" && value.kind_case() == ProtobufWkt::Value::kBoolValue) {
      test_options_.sync_mode_ = value.bool_value();
    }
    if (value_it.first == "crypto_error" && value.kind_case() == ProtobufWkt::Value::kBoolValue) {
      test_options_.crypto_error_ = value.bool_value();
    }
    if (value_it.first == "method_error" && value.kind_case() == ProtobufWkt::Value::kBoolValue) {
      test_options_.method_error_ = value.bool_value();
    }
    if (value_it.first == "async_method_error" &&
        value.kind_case() == ProtobufWkt::Value::kBoolValue) {
      test_options_.async_method_error_ = value.bool_value();
    }
    if (value_it.first == "expected_operation" &&
        value.kind_case() == ProtobufWkt::Value::kStringValue) {
      if (value.string_value() == "decrypt") {
        test_options_.decrypt_expected_ = true;
      } else if (value.string_value() == "sign") {
        test_options_.sign_expected_ = true;
      }
    }
  }

  std::string private_key = factory_context.api().fileSystem().fileReadToEnd(private_key_path);
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(const_cast<char*>(private_key.data()), private_key.size()));
  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    throw EnvoyException("Failed to read private key from disk.");
  }

  if (EVP_PKEY_id(pkey.get()) != EVP_PKEY_RSA) {
    throw EnvoyException("Private key is of wrong type.");
  }

  pkey_ = std::move(pkey);

  method_ = std::make_shared<SSL_PRIVATE_KEY_METHOD>();
  method_->sign = privateKeySign;
  method_->decrypt = privateKeyDecrypt;
  method_->complete = privateKeyComplete;
}

} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
