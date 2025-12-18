#include <openssl/ssl.h>
#include <ossl.h>
#include "internal.h"


/*
 * https://github.com/google/boringssl/blob/955ef7991e41ac6c0ea5114b4b9abb98cc5fd614/include/openssl/ssl.h#L1714
 * https://www.openssl.org/docs/man3.0/man3/SSL_SESSION_get_protocol_version.html
 */
extern "C" const char *SSL_SESSION_get_version(const SSL_SESSION *session) {
  return TLS_VERSION_to_string(ossl.ossl_SSL_SESSION_get_protocol_version(session));
}
