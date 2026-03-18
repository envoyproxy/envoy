#include <openssl/bio.h>
#include <ossl.h>

#include "custom_bio.h"


static int custom_bio_read(BIO *b, char *data, int len) {
  if (UserFuncs *funcs = reinterpret_cast<UserFuncs*>(ossl.ossl_BIO_get_data(b))) {
    if (funcs->user_read) {
      int ret = funcs->user_read(b, data, len);
      if (ret == 0) {
        ossl.ossl_BIO_set_flags(b, ossl_BIO_FLAGS_IN_EOF);
      }
      return ret;
    }
  }
  return 0;
}

static long custom_bio_ctrl(BIO *b, int cmd, long num, void *ptr) {
  if (cmd == ossl_BIO_CTRL_EOF) {
    if (ossl.ossl_BIO_test_flags(b, ossl_BIO_FLAGS_IN_EOF) != 0) {
      return 1;
    }
  }

  UserFuncs *funcs = reinterpret_cast<UserFuncs*>(ossl.ossl_BIO_get_data(b));
  return (funcs && funcs->user_ctrl) ? funcs->user_ctrl(b, cmd, num, ptr) : 0;
}

extern "C" BIO_METHOD *BIO_meth_new(int type, const char *name) {
  ossl_BIO_METHOD *meth = ossl.ossl_BIO_meth_new(type, name);

  UserFuncs::add(meth);

  ossl.ossl_BIO_meth_set_read(meth, custom_bio_read);
  ossl.ossl_BIO_meth_set_ctrl(meth, custom_bio_ctrl);

  return meth;
}
