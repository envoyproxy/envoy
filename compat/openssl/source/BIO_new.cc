#include <openssl/bio.h>
#include <ossl.h>

#include "custom_bio.h"


extern "C" BIO *BIO_new(const BIO_METHOD *meth) {
  ossl_BIO *bio = ossl.ossl_BIO_new(meth);

  if(bio) {
    if (UserFuncs *funcs = UserFuncs::find(meth)) {
      ossl.ossl_BIO_set_data(bio, funcs);
    }
  }

  return bio;
}
