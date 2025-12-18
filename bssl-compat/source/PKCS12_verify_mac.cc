#include <openssl/pkcs8.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/225e8d39b50757af56e61cd0aa7958c56c487d54/include/openssl/pkcs8.h#L199
 * https://www.openssl.org/docs/man3.0/man3/PKCS12_verify_mac.html
 */
extern "C" int PKCS12_verify_mac(const PKCS12 *p12, const char *password, int password_len) {
  return ossl.ossl_PKCS12_verify_mac(const_cast<PKCS12*>(p12), password, password_len);
}
