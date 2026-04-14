#include <openssl/bio.h>
#include <ossl.h>


extern "C" uint64_t BIO_number_written(const BIO *bio) {
  return ossl.ossl_BIO_number_written(const_cast<BIO*>(bio));
}
