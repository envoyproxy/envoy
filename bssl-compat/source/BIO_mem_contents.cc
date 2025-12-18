#include <openssl/bio.h>
#include <ossl.h>


/*
 * OSSL: Doesn't exist
 * BSSL: https://github.com/google/boringssl/blob/cacb5526268191ab52e3a8b2d71f686115776646/src/include/openssl/bio.h#L391
 */
extern "C" int BIO_mem_contents(const BIO *bio, const uint8_t **out_contents, size_t *out_len) {
  auto len = ossl.ossl_BIO_get_mem_data(const_cast<BIO*>(bio), out_contents);

  if (len == 0) { // A 0 return means either error or empty
    if (ossl.ossl_BIO_ctrl_pending(const_cast<BIO*>(bio)) != 0) {
      return 0; // Not empty so must be error
    }
  }

  *out_len = len;
  return 1;
}
