#include <openssl/evp.h>
#include <ossl.h>
#include <openssl/bytestring.h>


/*
 * BSSL: https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/evp.h#L203-L211
 * OSSL: N/A
 */
extern "C" EVP_PKEY *EVP_parse_public_key(CBS *cbs) {
  const unsigned char* tmp = cbs->data;
  return ossl.ossl_d2i_PUBKEY_ex(NULL, &tmp, cbs->len, NULL, NULL);
}
