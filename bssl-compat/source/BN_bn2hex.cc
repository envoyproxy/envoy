#include <openssl/bn.h>
#include <ossl.h>
#include <ctype.h>


/*
 * BSSL: https://github.com/google/boringssl/blob/cacb5526268191ab52e3a8b2d71f686115776646/src/include/openssl/bn.h#L267
 * OSSL: https://www.openssl.org/docs/man1.1.1/man3/BN_bn2hex.html
 *
 * While the BoringSSL description doesn't mention how to free the resulting string,
 * looking at the source shows that it is allocated with OPENSSL_malloc(), so it
 * should be freed with OPENSSL_free(), consistent with what OpenSSL says.
 */
extern "C" char *BN_bn2hex(const BIGNUM *bn) {
  char *s = ossl.ossl_BN_bn2hex(bn);

  if (s) {
    for(int i = 0; s[i]; i++) {
      s[i] = tolower(s[i]);
    }
  }

  return s;
}
