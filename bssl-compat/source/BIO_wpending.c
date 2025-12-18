#include <assert.h>
#include <openssl/bio.h>
#include <ossl.h>



size_t BIO_wpending(const BIO *bio) {
  const long r = BIO_ctrl((BIO *)bio, BIO_CTRL_WPENDING, 0, NULL);
  assert(r >= 0);

  if (r < 0) {
    return 0;
  }
  return r;
}
