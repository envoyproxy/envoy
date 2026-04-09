#include <openssl/bio.h>
#include <ossl.h>

#include "custom_bio.h"


extern "C" void *BIO_get_data(BIO *bio) {
  static const int index = custom_bio_ex_data_index();
  return ossl.ossl_BIO_get_ex_data(bio, index);
}
