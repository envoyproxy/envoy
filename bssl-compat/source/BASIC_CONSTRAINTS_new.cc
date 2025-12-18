#include <openssl/rsa.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/cd0b767492199a82c7e362d1a117e8c3fef6b943/include/openssl/x509v3.h#L464
 * https://www.openssl.org/docs/man3.0/man3/BASIC_CONSTRAINTS_new.html
 */
extern "C" BASIC_CONSTRAINTS *BASIC_CONSTRAINTS_new() {
  return ossl.ossl_BASIC_CONSTRAINTS_new();
}
