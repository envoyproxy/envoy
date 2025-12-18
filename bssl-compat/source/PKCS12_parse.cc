#include <openssl/pkcs8.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/225e8d39b50757af56e61cd0aa7958c56c487d54/include/openssl/pkcs8.h#L187
 * https://www.openssl.org/docs/man3.0/man3/PKCS12_parse.html
 */
extern "C" int PKCS12_parse(const PKCS12 *p12, const char *password, EVP_PKEY **out_pkey, X509 **out_cert, STACK_OF(X509) **out_ca_certs) {
  return ossl.ossl_PKCS12_parse(const_cast<PKCS12*>(p12), password, out_pkey, out_cert, reinterpret_cast<ossl_STACK_OF(ossl_X509)**>(out_ca_certs));
}
