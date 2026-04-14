#include <openssl/ssl.h>
#include <ossl.h>


extern "C" SSL_CTX *SSL_CTX_new(const SSL_METHOD *method) {
  ossl_SSL_CTX *ctx = ossl.ossl_SSL_CTX_new(method);

  if (ctx) {
    // This option is set so that we match BoringSSL's behavior... When this
    // option is enabled, if a peer just cloes its connection without sending a
    // close_notify alert, it will be treated as if the close_notify alert was
    // received, rather than treating it as an error.
    ossl.ossl_SSL_CTX_set_options(ctx, ossl_SSL_OP_IGNORE_UNEXPECTED_EOF);

    // Unless the user explicitly calls ossl_SSL_CTX_set1_cert_comp_preference()
    // and/or ossl_SSL_CTX_compress_certs(), OpenSSL will advertise all of its
    // builtin compression algorithms with some default preference order.
    // BoringSSL does the opposite, only advertising certificate comression
    // algorithms that the client has explicitly configured via
    // SSL_CTX_add_cert_compression_alg(). Therefore, we make this call to
    // ensure that all SSL_CTXs start life with no certificate compression
    // algorithms implicitly enabled, to match BoringSSL's behavior.
    ossl.ossl_SSL_CTX_set1_cert_comp_preference(ctx, nullptr, 0);
  }

  return ctx;
}
