#include <openssl/crypto.h>
#include <ossl.h>


extern "C" int FIPS_mode(void) {
  return ossl.ossl_EVP_default_properties_is_fips_enabled(NULL);
}
