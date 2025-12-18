#include <openssl/evp.h>
#include <ossl.h>
#include "log.h"


/*
 * BSSL: https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/evp.h#L135-L137
 * OSSL: https://www.openssl.org/docs/man3.0/man3/EVP_PKEY_id.html
 */
extern "C" int EVP_PKEY_id(const EVP_PKEY *pkey) {
  int ossl_id = ossl.ossl_EVP_PKEY_base_id(pkey);
  switch(ossl_id) {
    case ossl_EVP_PKEY_DSA: return EVP_PKEY_DSA;
    case ossl_EVP_PKEY_EC: return EVP_PKEY_EC;
    case ossl_EVP_PKEY_ED25519: return EVP_PKEY_ED25519;
    case ossl_EVP_PKEY_HKDF: return EVP_PKEY_HKDF;
    case ossl_EVP_PKEY_NONE: return EVP_PKEY_NONE;
    case ossl_EVP_PKEY_RSA: return EVP_PKEY_RSA;
    case ossl_EVP_PKEY_RSA_PSS: return EVP_PKEY_RSA_PSS;
    case ossl_EVP_PKEY_X25519: return EVP_PKEY_X25519;

    case ossl_EVP_PKEY_RSA2:
    case ossl_EVP_PKEY_DSA1:
    case ossl_EVP_PKEY_DSA2:
    case ossl_EVP_PKEY_DSA3:
    case ossl_EVP_PKEY_DSA4:
    case ossl_EVP_PKEY_DH:
    case ossl_EVP_PKEY_DHX:
    case ossl_EVP_PKEY_SM2:
    case ossl_EVP_PKEY_HMAC:
    case ossl_EVP_PKEY_CMAC:
    case ossl_EVP_PKEY_SCRYPT:
    case ossl_EVP_PKEY_TLS1_PRF:
    case ossl_EVP_PKEY_POLY1305:
    case ossl_EVP_PKEY_SIPHASH:
    case ossl_EVP_PKEY_X448:
    case ossl_EVP_PKEY_ED448: {
      bssl_compat_error("Cannot convert ossl_EVP_PKEY_base_id() value %d", ossl_id);
      return EVP_PKEY_NONE;
    }
    default: {
      bssl_compat_error("Unknown ossl_EVP_PKEY_base_id() value %d", ossl_id);
      return EVP_PKEY_NONE;
    }
  }
}
