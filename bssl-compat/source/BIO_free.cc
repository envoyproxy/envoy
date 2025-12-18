#include <openssl/bio.h>
#include <ossl.h>


/*
 * OSSL: https://www.openssl.org/docs/man1.1.1/man3/BIO_free.html
 * BSSL: https://github.com/google/boringssl/blob/cacb5526268191ab52e3a8b2d71f686115776646/src/include/openssl/bio.h#L86-L92
 */
extern "C" int BIO_free(BIO *bio) {
  // In BoringSSL, the BIO_free() call frees the whole chain,
  // whereas in OpenSSL, it just frees the single bio.
  ossl.ossl_BIO_free_all(bio);
  return 1;
}
