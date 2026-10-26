#include <openssl/ssl.h>
#include <ossl.h>

/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L867
 * https://www.openssl.org/docs/man3.0/man3/SSL_CTX_use_PrivateKey.html
 */
extern "C" int SSL_CTX_use_PrivateKey(SSL_CTX* ctx, EVP_PKEY* pkey) {
  // OpenSSL keeps a separate certificate/key slot per key type, and only checks
  // the key against the certificate in its own slot. A key whose type differs
  // from the loaded certificate is therefore accepted, and makes the empty slot
  // current, so SSL_CTX_get0_certificate() then returns nullptr. BoringSSL
  // rejects any key that doesn't match the loaded certificate, so do the same.
  if (pkey != nullptr) {
    X509* cert = ossl.ossl_SSL_CTX_get0_certificate(ctx);
    if (cert != nullptr && ossl.ossl_X509_check_private_key(cert, pkey) != 1) {
      return 0;
    }
  }
  return (ossl.ossl_SSL_CTX_use_PrivateKey(ctx, pkey) == 1) ? 1 : 0;
}
