#include <openssl/hmac.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/ec476ef0441f32fbcab558127412461617516336/include/openssl/hmac.h#L117
 * https://www.openssl.org/docs/man3.0/man3/HMAC_Init_ex.html
 */
extern "C" int HMAC_Init_ex(HMAC_CTX *ctx, const void *key, size_t key_len, const EVP_MD *md, ENGINE *impl) {
  if (md == nullptr) {
    // For a non-initial call, |md| may be NULL, in which
    // case the previous hash function will be used.
    md = ossl.ossl_HMAC_CTX_get_md(ctx);
    if (md == nullptr) {
      return 0;
    }
  }

  if (key == nullptr) {
    // A null key can be interpreted as either a blank key, if this is an
    // initial call, or alternatively as an indication to reuse the previous
    // key, if this is a non-initial call.
    if (ossl.ossl_HMAC_CTX_get_md(ctx) == nullptr) {
      // OpenSSL doesn't like a null key in initial
      // calls, so pass a blank instead.
      key = "";
      key_len = 0;
    }
  }

  return ossl.ossl_HMAC_Init_ex(ctx, key, key_len, md, impl);
}
