#include <openssl/bio.h>
#include <ossl.h>


/*
 * OSSL: https://github.com/openssl/openssl/blob/ac3cef223a4c61d6bee34527b6d4c8c6432494a7/include/openssl/bio.h#L721
 * OSSL: https://www.openssl.org/docs/man1.1.1/man3/BIO_new_bio_pair.html
 * BSSL: https://github.com/google/boringssl/blob/cacb5526268191ab52e3a8b2d71f686115776646/src/include/openssl/bio.h#L616
 */
extern "C" int BIO_new_bio_pair(BIO **out1, size_t writebuf1, BIO **out2, size_t writebuf2) {
  int rc = ossl.ossl_BIO_new_bio_pair(out1, writebuf1, out2, writebuf2);
  if (rc == 0) {
    *out1 = nullptr;
    *out2 = nullptr;
  }
  return rc;
}
