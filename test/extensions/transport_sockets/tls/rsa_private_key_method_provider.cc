/* Copyright (c) 2014, Google Inc.
 * Copyright (c) 2019, Intel Corp.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE. */

// The RSA signing and decrypting code has been adapted from the
// reference implementation in BoringSSL tests
// (https://github.com/google/boringssl/blob/master/ssl/test/test_config.cc).
// The license for this file is thus the same as the license for the
// source file.

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
    this->cb_.complete(Envoy::Ssl::PrivateKeyMethodStatus::Success);
    return;
  });
  timer_->enableTimer(timeout_0ms);
}

static ssl_private_key_result_t privateKeySign(SSL* ssl, uint8_t* out, size_t* out_len,
                                               size_t max_out, uint16_t signature_algorithm,
                                               const uint8_t* in, size_t in_len) {
  (void)out;
  (void)out_len;
  size_t len = 0;
  EVP_PKEY_CTX* pkey_ctx;
  const EVP_MD* digest;
  bssl::ScopedEVP_MD_CTX md_ctx;
  RsaPrivateKeyConnection* ops = static_cast<RsaPrivateKeyConnection*>(
      SSL_get_ex_data(ssl, RsaPrivateKeyMethodProvider::ssl_rsa_connection_index));

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

  EVP_PKEY* rsa_pkey = ops->getPrivateKey();
  if (!rsa_pkey) {
    return ssl_private_key_failure;
  }

  // Get the digest algorithm.
  digest = SSL_get_signature_algorithm_digest(signature_algorithm);
  if (digest == NULL) {
    return ssl_private_key_failure;
  }

  // Initialize the signing context.
  if (!EVP_DigestSignInit(md_ctx.get(), &pkey_ctx, digest, nullptr, rsa_pkey)) {
    return ssl_private_key_failure;
  }

  // Set options for PSS.
  if (SSL_is_signature_algorithm_rsa_pss(signature_algorithm)) {
    if (!EVP_PKEY_CTX_set_rsa_padding(pkey_ctx, RSA_PKCS1_PSS_PADDING)) {
      return ssl_private_key_failure;
    }
    if (!EVP_PKEY_CTX_set_rsa_pss_saltlen(pkey_ctx, -1)) {
      return ssl_private_key_failure;
    }
  }

  // Get the signature length for memory allocation.
  if (!EVP_DigestSign(md_ctx.get(), nullptr, &len, in, in_len)) {
    return ssl_private_key_failure;
  }

  if (len == 0 || len > max_out) {
    return ssl_private_key_failure;
  }

  ops->out_len_ = len;
  ops->out_ = static_cast<uint8_t*>(OPENSSL_malloc(len));
  if (ops->out_ == nullptr) {
    return ssl_private_key_failure;
  }

  // Run the signing operation.
  if (!EVP_DigestSign(md_ctx.get(), ops->out_, &len, in, in_len)) {
    OPENSSL_free(ops->out_);
    return ssl_private_key_failure;
  }

  if (ops->test_options_.crypto_error_) {
    // Flip the bits in the first byte to cause the handshake to fail.
    ops->out_[0] ^= ops->out_[0];
  }

  if (ops->test_options_.sync_mode_) {
    // Return immediately with the results.
    memcpy(out, ops->out_, ops->out_len_);
    *out_len = ops->out_len_;
    OPENSSL_free(ops->out_);
    return ssl_private_key_success;
  }

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

  EVP_PKEY* rsa_pkey = ops->getPrivateKey();
  if (!rsa_pkey) {
    return ssl_private_key_failure;
  }

  rsa = EVP_PKEY_get0_RSA(rsa_pkey);
  if (rsa == NULL) {
    return ssl_private_key_failure;
  }

  ops->out_ = static_cast<uint8_t*>(OPENSSL_malloc(max_out));
  if (ops->out_ == nullptr) {
    return ssl_private_key_failure;
  }

  if (!RSA_decrypt(rsa, &ops->out_len_, ops->out_, max_out, in, in_len, RSA_NO_PADDING)) {
    OPENSSL_free(ops->out_);
    return ssl_private_key_failure;
  }

  if (ops->test_options_.sync_mode_) {
    // Return immediately with the results.
    memcpy(out, ops->out_, ops->out_len_);
    *out_len = ops->out_len_;
    OPENSSL_free(ops->out_);
    return ssl_private_key_success;
  }

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
    OPENSSL_free(ops->out_);
    return ssl_private_key_failure;
  }

  if (ops->out_len_ > max_out) {
    OPENSSL_free(ops->out_);
    return ssl_private_key_failure;
  }

  memcpy(out, ops->out_, ops->out_len_);
  *out_len = ops->out_len_;

  OPENSSL_free(ops->out_);

  return ssl_private_key_success;
}

Ssl::BoringSslPrivateKeyMethodSharedPtr
RsaPrivateKeyMethodProvider::getBoringSslPrivateKeyMethod() {
  return method_;
}

RsaPrivateKeyConnection::RsaPrivateKeyConnection(SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb,
                                                 Event::Dispatcher& dispatcher,
                                                 bssl::UniquePtr<EVP_PKEY> pkey,
                                                 RsaPrivateKeyConnectionTestOptions& test_options)
    : test_options_(test_options), cb_(cb), dispatcher_(dispatcher), pkey_(move(pkey)) {
  SSL_set_ex_data(ssl, RsaPrivateKeyMethodProvider::ssl_rsa_connection_index, this);
}

Ssl::PrivateKeyConnectionPtr RsaPrivateKeyMethodProvider::getPrivateKeyConnection(
    SSL* ssl, Ssl::PrivateKeyConnectionCallbacks& cb, Event::Dispatcher& dispatcher) {
  bssl::UniquePtr<BIO> bio(
      BIO_new_mem_buf(const_cast<char*>(private_key_.data()), private_key_.size()));
  bssl::UniquePtr<EVP_PKEY> pkey(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
  if (pkey == nullptr) {
    return nullptr;
  }

  return std::make_unique<RsaPrivateKeyConnection>(ssl, cb, dispatcher, move(pkey), test_options_);
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

  private_key_ = factory_context.api().fileSystem().fileReadToEnd(private_key_path);

  method_ = std::make_shared<SSL_PRIVATE_KEY_METHOD>();
  method_->sign = privateKeySign;
  method_->decrypt = privateKeyDecrypt;
  method_->complete = privateKeyComplete;
}

} // namespace PrivateKeyMethodProvider
} // namespace Extensions
} // namespace Envoy
