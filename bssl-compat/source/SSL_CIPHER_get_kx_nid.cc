#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/955ef7991e41ac6c0ea5114b4b9abb98cc5fd614/include/openssl/ssl.h#L1451
 * https://www.openssl.org/docs/man3.0/man3/SSL_CIPHER_get_kx_nid.html
 */
extern "C" int SSL_CIPHER_get_kx_nid(const SSL_CIPHER *cipher) {
  int nid {ossl.ossl_SSL_CIPHER_get_kx_nid(cipher)};

#ifndef NID_kx_ecdhe_psk
  if (nid == ossl_NID_kx_ecdhe_psk) {
    return NID_kx_ecdhe;
  }
#endif

  return nid;
}
