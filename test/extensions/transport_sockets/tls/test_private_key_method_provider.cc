#include "test/extensions/transport_sockets/tls/test_private_key_method_provider.h"

#include <memory>

#include "envoy/api/api.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace PrivateKeyMethodProvider {

void TestPrivateKeyConnection::delayed_op() {
  const std::chrono::milliseconds timeout_0ms{0};

  timer_ = dispatcher_.createTimer([this]() -> void {
    finished_ = true;
    this->cb_.onPrivateKeyMethodComplete();
  });
  timer_->enableTimer(timeout_0ms);
}

static int calculateDigest(const EVP_MD* md, const uint8_t* in, size_t in_len, unsigned char* hash,
                           unsigned int* hash_len) {
  bssl::ScopedEVP_MD_CTX ctx;

  // Calculate the message digest for signing.
  if (!EVP_DigestInit_ex(ctx.get(), md, nullptr) || !EVP_DigestUpdate(ctx.get(), in, in_len) ||
      !EVP_DigestFinal_ex(ctx.get(), hash, hash_len)) {
    return 0;
  }
  return 1;
}

static ssl_private_key_result_t ecdsaPrivateKeySign(SSL* ssl, uint8_t* out, size_t* out_len,
                                                    size_t max_out, uint16_t signature_algorithm,
                                                    const uint8_t* in, size_t in_len) {
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len;
  TestPrivateKeyConnection* ops = static_cast<TestPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, TestPrivateKeyMethodProvider::ecdsaConnectionIndex()));
  unsigned int out_len_unsigned;

  if (!ops) {
    return ssl_private_key_failure;
  }

  if (ops->test_options_.method_error_) {
    // Have an artificial test failure.
    return ssl_private_key_failure;
  }

  if (!ops->test_options_.sign_expected_) {
    return ssl_private_key_failure;
  }

  const EVP_MD* md = SSL_get_signature_algorithm_digest(signature_algorithm);
  if (!md) {
    return ssl_private_key_failure;
  }

  if (!calculateDigest(md, in, in_len, hash, &hash_len)) {
    return ssl_private_key_failure;
  }

  bssl::UniquePtr<EC_KEY> ec_key(EVP_PKEY_get1_EC_KEY(ops->getPrivateKey()));
  if (!ec_key) {
    return ssl_private_key_failure;
  }

  // Borrow "out" because it has been already initialized to the max_out size.
  if (!ECDSA_sign(0, hash, hash_len, out, &out_len_unsigned, ec_key.get())) {
    return ssl_private_key_failure;
  }

  if (ops->test_options_.sync_mode_) {
    // Return immediately with the results.
    if (out_len_unsigned > max_out) {
      return ssl_private_key_failure;
    }
    *out_len = out_len_unsigned;
    return ssl_private_key_success;
  }

  ops->output_.assign(out, out + out_len_unsigned);
  // Tell SSL socket that the operation is ready to be called again.
  ops->delayed_op();

  return ssl_private_key_retry;
}

static ssl_private_key_result_t ecdsaPrivateKeyDecrypt(SSL*, uint8_t*, size_t*, size_t,
                                                       const uint8_t*, size_t) {
  return ssl_private_key_failure;
}

static ssl_private_key_result_t rsaPrivateKeySign(SSL* ssl, uint8_t* out, size_t* out_len,
                                                  size_t max_out, uint16_t signature_algorithm,
                                                  const uint8_t* in, size_t in_len) {
  TestPrivateKeyConnection* ops = static_cast<TestPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, TestPrivateKeyMethodProvider::rsaConnectionIndex()));
  unsigned char hash[EVP_MAX_MD_SIZE] = {0};
  unsigned int hash_len = EVP_MAX_MD_SIZE;
  std::vector<uint8_t> in2;

  if (!ops) {
    return ssl_private_key_failure;
  }

  if (ops->test_options_.method_error_) {
    return ssl_private_key_failure;
  }

  if (!ops->test_options_.sign_expected_) {
    return ssl_private_key_failure;
  }

  const EVP_MD* md = SSL_get_signature_algorithm_digest(signature_algorithm);
  if (!md) {
    return ssl_private_key_failure;
  }

  in2.assign(in, in + in_len);

  // If crypto error is set, we'll modify the incoming token by flipping
  // the bits.
  if (ops->test_options_.crypto_error_) {
    for (size_t i = 0; i < in_len; i++) {
      in2[i] = ~in2[i];
    }
  }

  if (!calculateDigest(md, in2.data(), in_len, hash, &hash_len)) {
    return ssl_private_key_failure;
  }

  RSA* rsa = EVP_PKEY_get0_RSA(ops->getPrivateKey());
  if (rsa == nullptr) {
    return ssl_private_key_failure;
  }

  // Perform RSA signing.
  if (SSL_is_signature_algorithm_rsa_pss(signature_algorithm)) {
    if (!RSA_sign_pss_mgf1(rsa, out_len, out, max_out, hash, hash_len, md, nullptr, -1)) {
      return ssl_private_key_failure;
    }
  } else {
    unsigned int out_len_unsigned;
    if (!RSA_sign(EVP_MD_type(md), hash, hash_len, out, &out_len_unsigned, rsa)) {
      return ssl_private_key_failure;
    }
    if (out_len_unsigned > max_out) {
      return ssl_private_key_failure;
    }
    *out_len = out_len_unsigned;
  }

  if (ops->test_options_.sync_mode_) {
    return ssl_private_key_success;
  }

  ops->output_.assign(out, out + *out_len);
  ops->delayed_op();

  return ssl_private_key_retry;
}

static ssl_private_key_result_t rsaPrivateKeyDecrypt(SSL* ssl, uint8_t* out, size_t* out_len,
                                                     size_t max_out, const uint8_t* in,
                                                     size_t in_len) {
  TestPrivateKeyConnection* ops = static_cast<TestPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, TestPrivateKeyMethodProvider::rsaConnectionIndex()));

  if (!ops) {
    return ssl_private_key_failure;
  }

  if (ops->test_options_.method_error_) {
    return ssl_private_key_failure;
  }

  if (!ops->test_options_.decrypt_expected_) {
    return ssl_private_key_failure;
  }

  RSA* rsa = EVP_PKEY_get0_RSA(ops->getPrivateKey());
  if (rsa == nullptr) {
    return ssl_private_key_failure;
  }

  if (!RSA_decrypt(rsa, out_len, out, max_out, in, in_len, RSA_NO_PADDING)) {
    return ssl_private_key_failure;
  }

  if (ops->test_options_.sync_mode_) {
    return ssl_private_key_success;
  }

  ops->output_.assign(out, out + *out_len);
  ops->delayed_op();

  return ssl_private_key_retry;
}

static ssl_private_key_result_t privateKeyComplete(SSL* ssl, uint8_t* out, size_t* out_len,
                                                   size_t max_out, int id) {
  TestPrivateKeyConnection* ops = static_cast<TestPrivateKeyConnection*>(SSL_get_ex_data(ssl, id));

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

static ssl_private_key_result_t rsaPrivateKeyComplete(SSL* ssl, uint8_t* out, size_t* out_len,
                                                      size_t max_out) {
  return privateKeyComplete(ssl, out, out_len, max_out,
                            TestPrivateKeyMethodProvider::rsaConnectionIndex());
}

static ssl_private_key_result_t ecdsaPrivateKeyComplete(SSL* ssl, uint8_t* out, size_t* out_len,
                                                        size_t max_out) {
  return privateKeyComplete(ssl, out, out_len, max_out,
                            TestPrivateKeyMethodProvider::ecdsaConnectionIndex());
}

Ssl::BoringSslPrivateKeyMethodSharedPtr
TestPrivateKeyMethodProvider::getBoringSslPrivateKeyMethod() {
  return method_;
}

bool TestPrivateKeyMethodProvider::checkFips() {
  if (mode_ == "rsa") {
    RSA* rsa_private_key = EVP_PKEY_get0_RSA(pkey_.get());
    if (rsa_private_key == nullptr || !RSA_check_fips(rsa_private_key)) {
      return false;
    }
  } else { // if (mode_ == "ecdsa")
    const EC_KEY* ecdsa_private_key = EVP_PKEY_get0_EC_KEY(pkey_.get());
    if (ecdsa_private_key == nullptr || !EC_KEY_check_fips(ecdsa_private_key)) {
      return false;
    }
  }
  return true;
}

TestPrivateKeyConnection::TestPrivateKeyConnection(
    Ssl::PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher,
    bssl::UniquePtr<EVP_PKEY> pkey, TestPrivateKeyConnectionTestOptions& test_options)
    : test_options_(test_options), cb_(cb), dispatcher_(dispatcher), pkey_(std::move(pkey)) {}

void TestPrivateKeyMethodProvider::registerPrivateKeyMethod(SSL* ssl,
                                                            Ssl::PrivateKeyConnectionCallbacks& cb,
                                                            Event::Dispatcher& dispatcher) {
  TestPrivateKeyConnection* ops;
  // In multi-cert case, when the same provider is used in different modes with the same SSL object,
  // we need to keep both rsa and ecdsa connection objects in store because the test options for the
  // two certificates may be different. We need to be able to deduct in the signing, decryption, and
  // completion functions which options to use, so we associate the connection objects to the same
  // SSL object using different user data indexes.
  //
  // Another way to do this would be to store both test options in one connection object.
  int index = mode_ == "rsa" ? TestPrivateKeyMethodProvider::rsaConnectionIndex()
                             : TestPrivateKeyMethodProvider::ecdsaConnectionIndex();

  // Check if there is another certificate of the same mode associated with the context. This would
  // be an error.
  ops = static_cast<TestPrivateKeyConnection*>(SSL_get_ex_data(ssl, index));
  if (ops != nullptr) {
    throw EnvoyException(
        "Can't distinguish between two registered providers for the same SSL object.");
  }

  ops = new TestPrivateKeyConnection(cb, dispatcher, bssl::UpRef(pkey_), test_options_);
  SSL_set_ex_data(ssl, index, ops);
}

void TestPrivateKeyMethodProvider::unregisterPrivateKeyMethod(SSL* ssl) {
  int index = mode_ == "rsa" ? TestPrivateKeyMethodProvider::rsaConnectionIndex()
                             : TestPrivateKeyMethodProvider::ecdsaConnectionIndex();
  TestPrivateKeyConnection* ops =
      static_cast<TestPrivateKeyConnection*>(SSL_get_ex_data(ssl, index));
  SSL_set_ex_data(ssl, index, nullptr);
  delete ops;
}

static int createIndex() {
  int index = SSL_get_ex_new_index(0, nullptr, nullptr, nullptr, nullptr);
  RELEASE_ASSERT(index >= 0, "Failed to get SSL user data index.");
  return index;
}

int TestPrivateKeyMethodProvider::rsaConnectionIndex() {
  CONSTRUCT_ON_FIRST_USE(int, createIndex());
}

int TestPrivateKeyMethodProvider::ecdsaConnectionIndex() {
  CONSTRUCT_ON_FIRST_USE(int, createIndex());
}

TestPrivateKeyMethodProvider::TestPrivateKeyMethodProvider(
    const ProtobufWkt::Any& typed_config,
    Server::Configuration::TransportSocketFactoryContext& factory_context) {
  std::string private_key_path;

  auto config = MessageUtil::anyConvert<ProtobufWkt::Struct>(typed_config);

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
    if (value_it.first == "mode" && value.kind_case() == ProtobufWkt::Value::kStringValue) {
      mode_ = value.string_value();
    }
  }

  std::string private_key = factory_context.api().fileSystem().fileReadToEnd(private_key_path);
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(const_cast<char*>(private_key.data()), private_key.size()));
  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    throw EnvoyException("Failed to read private key from disk.");
  }

  method_ = std::make_shared<SSL_PRIVATE_KEY_METHOD>();

  // Have two modes, "rsa" and "ecdsa", for testing multi-cert use cases.
  if (mode_ == "rsa") {
    if (EVP_PKEY_id(pkey.get()) != EVP_PKEY_RSA) {
      throw EnvoyException("Private key is not RSA.");
    }
    method_->sign = rsaPrivateKeySign;
    method_->decrypt = rsaPrivateKeyDecrypt;
    method_->complete = rsaPrivateKeyComplete;
  } else if (mode_ == "ecdsa") {
    if (EVP_PKEY_id(pkey.get()) != EVP_PKEY_EC) {
      throw EnvoyException("Private key is not ECDSA.");
    }
    method_->sign = ecdsaPrivateKeySign;
    method_->decrypt = ecdsaPrivateKeyDecrypt;
    method_->complete = ecdsaPrivateKeyComplete;
  } else {
    throw EnvoyException("Unknown test provider mode, supported modes are \"rsa\" and \"ecdsa\".");
  }

  pkey_ = std::move(pkey);
}

} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
