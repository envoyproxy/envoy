#include <openssl/bio.h>
#include <ossl.h>

#include "custom_bio.h"


extern "C" void BIO_set_data(BIO *bio, void *ptr) {
  static const int index = custom_bio_ex_data_index();
  ossl.ossl_BIO_set_ex_data(bio, index, ptr);
}
