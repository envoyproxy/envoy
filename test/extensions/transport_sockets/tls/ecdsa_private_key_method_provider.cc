#include "test/extensions/transport_sockets/tls/ecdsa_private_key_method_provider.h"

#include <memory>

#include "envoy/api/api.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {

int EcdsaPrivateKeyMethodProvider::ssl_ecdsa_connection_index = -1;

void EcdsaPrivateKeyConnection::delayed_op() {
  const std::chrono::milliseconds timeout_0ms{0};

  timer_ = dispatcher_.createTimer([this]() -> void {
    finished_ = true;
    this->cb_.complete();
    return;
  });
  timer_->enableTimer(timeout_0ms);
}

static ssl_private_key_result_t privateKeySign(SSL* ssl, uint8_t* out, size_t* out_len,
                                               size_t max_out, uint16_t signature_algorithm,
                                               const uint8_t* in, size_t in_len) {
  (void)out_len;
  (void)max_out;
  (void)signature_algorithm;
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;
  const EVP_MD* md;
  bssl::ScopedEVP_MD_CTX ctx;
  EcdsaPrivateKeyConnection* ops = static_cast<EcdsaPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, EcdsaPrivateKeyMethodProvider::ssl_ecdsa_connection_index));
  unsigned int result_len;

  if (!ops) {
    return ssl_private_key_failure;
  }

  bssl::UniquePtr<EC_KEY> ec_key(ops->getPrivateKey());
  if (!ec_key) {
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

  // Borrow "out" because it has been already initialized to the max_out size.
  if (!ECDSA_sign(0, hash, hash_len, out, &result_len, ec_key.get())) {
    return ssl_private_key_failure;
  }

  ops->output_.assign(out, out + result_len);

  // Tell SSL socket that the operation is ready to be called again.
  ops->delayed_op();

  return ssl_private_key_retry;
}

static ssl_private_key_result_t privateKeyDecrypt(SSL* ssl, uint8_t* out, size_t* out_len,
                                                  size_t max_out, const uint8_t* in,
                                                  size_t in_len) {
  (void)ssl;
  (void)out;
  (void)out_len;
  (void)max_out;
  (void)in;
  (void)in_len;

  return ssl_private_key_failure;
}

static ssl_private_key_result_t privateKeyComplete(SSL* ssl, uint8_t* out, size_t* out_len,
                                                   size_t max_out) {
  EcdsaPrivateKeyConnection* ops = static_cast<EcdsaPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, EcdsaPrivateKeyMethodProvider::ssl_ecdsa_connection_index));

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
EcdsaPrivateKeyMethodProvider::getBoringSslPrivateKeyMethod() {
  return method_;
}

EcdsaPrivateKeyConnection::EcdsaPrivateKeyConnection(
    SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher,
    bssl::UniquePtr<EVP_PKEY> pkey, EcdsaPrivateKeyConnectionTestOptions& test_options)
    : test_options_(test_options), cb_(cb), dispatcher_(dispatcher), pkey_(move(pkey)) {
  SSL_set_ex_data(ssl, EcdsaPrivateKeyMethodProvider::ssl_ecdsa_connection_index, this);
}

Ssl::PrivateKeyConnectionPtr EcdsaPrivateKeyMethodProvider::getPrivateKeyConnection(
    SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher) {
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(const_cast<char*>(private_key_.data()), private_key_.size()));
  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    return nullptr;
  }

  return std::make_unique<EcdsaPrivateKeyConnection>(ssl, cb, dispatcher, move(pkey),
                                                     test_options_);
}

EcdsaPrivateKeyMethodProvider::EcdsaPrivateKeyMethodProvider(
    const ProtobufWkt::Struct& config,
    Server::Configuration::TransportSocketFactoryContext& factory_context) {

  std::string private_key_path;

  if (EcdsaPrivateKeyMethodProvider::ssl_ecdsa_connection_index == -1) {
    EcdsaPrivateKeyMethodProvider::ssl_ecdsa_connection_index =
        SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  }

  for (auto& value_it : config.fields()) {
    auto& value = value_it.second;
    if (value_it.first == "private_key_file" &&
        value.kind_case() == ProtobufWkt::Value::kStringValue) {
      private_key_path = value.string_value();
    }
    if (value_it.first == "async_method_error" &&
        value.kind_case() == ProtobufWkt::Value::kBoolValue) {
      test_options_.async_method_error_ = value.bool_value();
    }
  }

  private_key_ = factory_context.api().fileSystem().fileReadToEnd(private_key_path);

  method_ = std::make_shared<SSL_PRIVATE_KEY_METHOD>();
  method_->sign = privateKeySign;
  method_->decrypt = privateKeyDecrypt;
  method_->complete = privateKeyComplete;
}

} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
