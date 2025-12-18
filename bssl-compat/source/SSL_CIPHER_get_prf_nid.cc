#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/955ef7991e41ac6c0ea5114b4b9abb98cc5fd614/include/openssl/ssl.h#L1372
 * https://www.openssl.org/docs/man3.0/man3/SSL_CIPHER_get_handshake_digest.html
 */
extern "C" int SSL_CIPHER_get_prf_nid(const SSL_CIPHER *cipher) {
  return ossl.ossl_EVP_MD_get_type(ossl.ossl_SSL_CIPHER_get_handshake_digest(cipher));
}
