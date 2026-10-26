#include <openssl/bio.h>
#include <ossl.h>

extern "C" uint64_t BIO_number_read(const BIO* bio) {
  return ossl_BIO_number_read(const_cast<BIO*>(bio));
}
