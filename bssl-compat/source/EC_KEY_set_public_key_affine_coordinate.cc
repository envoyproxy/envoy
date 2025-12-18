#include <openssl/ec_key.h>
#include <openssl/bytestring.h>
#include <ossl.h>
#include "log.h"


extern "C" int EC_KEY_set_public_key_affine_coordinates(EC_KEY *key,
                                                          const BIGNUM *x,
                                                          const BIGNUM *y) {
  // NOTE: BoringSSL and OpenSSL as of version 3.0 have binary compatability sharing BIGNUM
  // so no conversion need be performed apart from removing 'const'.
  return  ossl.ossl_EC_KEY_set_public_key_affine_coordinates(key,
                                                             const_cast<BIGNUM *>(x),
                                                             const_cast<BIGNUM *>(y));
}
