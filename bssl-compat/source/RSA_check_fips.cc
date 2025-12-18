#include <openssl/rsa.h>
#include "log.h"


extern "C" int RSA_check_fips(RSA *key) {
  bssl_compat_fatal("%s() NYI", __func__);
  return 0;
}
