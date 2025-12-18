#include <openssl/ssl.h>
#include <ossl.h>
#include "log.h"


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L2414
 * https://www.openssl.org/docs/man3.0/man3/SSL_set_verify.html
 */
extern "C" void SSL_set_verify(SSL *ssl, int mode, int (*callback)(int ok, X509_STORE_CTX *store_ctx)) {
  if(callback) {
    bssl_compat_fatal("%s() : non-null callback is not supported", __func__);
  }
  ossl.ossl_SSL_set_verify(ssl, mode, callback);
}

