#include <openssl/rsa.h>
#include "log.h"


extern "C" int RSA_sign_pss_mgf1(RSA *rsa, size_t *out_len, uint8_t *out,
                                     size_t max_out, const uint8_t *digest,
                                     size_t digest_len, const EVP_MD *md,
                                     const EVP_MD *mgf1_md, int salt_len) {
  bssl_compat_fatal("%s() NYI", __func__);
  return 0;
}
