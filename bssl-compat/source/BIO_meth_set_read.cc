#include <openssl/bio.h>
#include <ossl.h>

/*
 * Simple hand-crafted wrapper that our scripts were not able to generate
 * because its signature contains a function pointer.
 * TODO: extend generation scripts to support function pointers
 */
extern "C" int BIO_meth_set_read(BIO_METHOD *biom, int (*read)(BIO *, char *, int)) {
  return ossl.ossl_BIO_meth_set_read(biom, read);
}
