#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <iostream>

#include "openssl/ssl.h"
#include "openssl/hmac.h"
#include "openssl/rand.h"
#include "openssl/x509v3.h"

#include "common/ssl/context_impl.h"

namespace Envoy {
namespace Ssl {

int alpnSelectCallback(std::vector<uint8_t> parsed_alpn_protocols,
                                    const unsigned char** out, unsigned char* outlen,
                                    const unsigned char* in, unsigned int inlen) {
  // Currently this uses the standard selection algorithm in priority order.
  const uint8_t* alpn_data = &parsed_alpn_protocols[0];
  size_t alpn_data_size = parsed_alpn_protocols.size();
  if (SSL_select_next_proto(const_cast<unsigned char**>(out), outlen, alpn_data, alpn_data_size, in,
                            inlen) != OPENSSL_NPN_NEGOTIATED) {
    return SSL_TLSEXT_ERR_NOACK;
  } else {
    return SSL_TLSEXT_ERR_OK;
  }
}

enum ssl_select_cert_result_t selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello) {
  // This is currently a nop, since we only have a single cert, but this is where we will implement
  // the certificate selection logic in #1319.
  //RELEASE_ASSERT(SSL_set_SSL_CTX(ssl_client_hello->ssl, tls_contexts_[0].ssl_ctx_.get()) != nullptr, "");

  return ssl_select_cert_success;
}

void set_select_certificate_cb(SSL_CTX *ctx ){
  enum ssl_select_cert_result_t selectTlsContext(const SSL_CLIENT_HELLO* ssl_client_hello);
  SSL_CTX_set_select_certificate_cb(
      ctx,
      [](const SSL_CLIENT_HELLO* client_hello) -> ssl_select_cert_result_t {
        return selectTlsContext(client_hello);
      });
}

//bssl::UniquePtr<SSL> newSsl(SSL_CTX *ctx) {
//  return bssl::UniquePtr<SSL>(SSL_new(ctx));
//}

int set_strict_cipher_list(SSL_CTX *ctx, const char *str) {
  return SSL_CTX_set_strict_cipher_list(ctx, str);
}

std::string getSerialNumberFromCertificate(X509* cert) {
  ASN1_INTEGER* serial_number = X509_get_serialNumber(cert);
  BIGNUM num_bn;
  BN_init(&num_bn);
  ASN1_INTEGER_to_BN(serial_number, &num_bn);
  char* char_serial_number = BN_bn2hex(&num_bn);
  BN_free(&num_bn);
  if (char_serial_number != nullptr) {
    std::string serial_number(char_serial_number);
    OPENSSL_free(char_serial_number);
    return serial_number;
  }
  return "";
}

void allowRenegotiation(SSL* ssl) {
  SSL_set_renegotiate_mode(ssl, ssl_renegotiate_freely);
}

bssl::UniquePtr<STACK_OF(X509_NAME)> initX509Names() {
  bssl::UniquePtr<STACK_OF(X509_NAME)> list(sk_X509_NAME_new(
        [](const X509_NAME** a, const X509_NAME** b) -> int { return X509_NAME_cmp(*a, *b); }));

  return list;
}

EVP_MD_CTX* newEVP_MD_CTX() {
  EVP_MD_CTX *md = EVP_MD_CTX_new();
  return md;
}

SSL_SESSION *ssl_session_from_bytes(SSL *client_ssl_socket, const SSL_CTX *client_ssl_context, const std::string& client_session) {
  return SSL_SESSION_from_bytes(reinterpret_cast<const uint8_t*>(client_session.data()),
                               client_session.size(), client_ssl_context);             
}

int ssl_session_to_bytes(const SSL_SESSION *in, uint8_t **out_data, size_t *out_len) {
   return SSL_SESSION_to_bytes(in, out_data, out_len);
}

X509* getVerifyCallbackCert(X509_STORE_CTX* store_ctx, void* arg) {
  SSL* ssl = reinterpret_cast<SSL*>(
      X509_STORE_CTX_get_ex_data(store_ctx, SSL_get_ex_data_X509_STORE_CTX_idx()));
  bssl::UniquePtr<X509> cert(SSL_get_peer_certificate(ssl));

  return cert.get();
}

int ssl_session_is_resumable(const SSL_SESSION* session) {
  return SSL_SESSION_is_resumable(session);
}

void ssl_ctx_add_client_CA(SSL_CTX *ctx, X509 *x) {

}

int should_be_single_use(const SSL_SESSION *session) {
  return SSL_SESSION_should_be_single_use(session);
}

} // namespace Ssl
} // namespace Envoy
