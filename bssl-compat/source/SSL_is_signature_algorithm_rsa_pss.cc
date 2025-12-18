#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/955ef7991e41ac6c0ea5114b4b9abb98cc5fd614/include/openssl/ssl.h#L1100
 */
extern "C" int SSL_is_signature_algorithm_rsa_pss(uint16_t sigalg) {
  switch (sigalg) {
    case SSL_SIGN_RSA_PSS_RSAE_SHA256: return 1;
    case SSL_SIGN_RSA_PSS_RSAE_SHA384: return 1;
    case SSL_SIGN_RSA_PSS_RSAE_SHA512: return 1;
  }

  return 0;
}
