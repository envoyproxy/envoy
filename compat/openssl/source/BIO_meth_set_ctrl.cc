#include <openssl/bio.h>
#include <ossl.h>

#include "custom_bio.h"


extern "C" int BIO_meth_set_ctrl(BIO_METHOD *meth, long (*ctrl)(BIO*, int, long, void*)) {
  UserFuncs::get(meth).user_ctrl = ctrl;
  return 1;
}
