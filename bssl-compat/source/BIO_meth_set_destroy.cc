#include <openssl/bio.h>
#include <ossl.h>

/*
 * Simple hand-crafted wrapper that our scripts were not able to generate
 * because its signature contains a function pointer.
 * TODO: extend generation scripts to support function pointers
 */
extern "C" int BIO_meth_set_destroy(BIO_METHOD *biom, int (*destroy)(BIO *)) {
  return ossl.ossl_BIO_meth_set_destroy(biom, destroy);
}
