#include <openssl/rsa.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/cd0b767492199a82c7e362d1a117e8c3fef6b943/include/openssl/x509v3.h#L464
 * https://www.openssl.org/docs/man3.0/man3/BASIC_CONSTRAINTS_free.html
 */
extern "C" void BASIC_CONSTRAINTS_free(BASIC_CONSTRAINTS *a) {
  ossl.ossl_BASIC_CONSTRAINTS_free(a);
}
