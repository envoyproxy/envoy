#include <openssl/ec_key.h>
#include "log.h"


extern "C" int EC_KEY_check_fips(const EC_KEY *key) {
  bssl_compat_fatal("%s() NYI", __func__);
  return 0;
}
