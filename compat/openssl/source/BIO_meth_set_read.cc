#include <openssl/bio.h>
#include <ossl.h>

#include "custom_bio.h"


extern "C" int BIO_meth_set_read(BIO_METHOD *meth, int (*read)(BIO*, char*, int)) {
  UserFuncs::get(meth).user_read = read;
  return 1;
}
