#include <openssl/bio.h>
#include <ossl.h>


extern "C" size_t BIO_pending(const BIO *bio) {
   return ossl_BIO_pending(const_cast<BIO*>(bio));
}
