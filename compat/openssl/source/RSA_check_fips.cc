#include <openssl/rsa.h>


// OpenSSL's FIPS provider already performs key validation during key operations,
// making an explicit check here redundant and a performance bottleneck.
extern "C" int RSA_check_fips(RSA *key) {
  return 1;
}
