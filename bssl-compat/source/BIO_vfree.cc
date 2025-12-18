#include <openssl/bio.h>
#include <ossl.h>


/*
 * OSSL: https://www.openssl.org/docs/man1.1.1/man3/BIO_vfree.html
 * BSSL: https://github.com/google/boringssl/blob/cacb5526268191ab52e3a8b2d71f686115776646/src/include/openssl/bio.h#L98
 */
extern "C" void BIO_vfree(BIO *bio) {
  ossl.ossl_BIO_free(bio);
}

