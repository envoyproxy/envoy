#include <openssl/bio.h>
#include <ossl.h>


extern "C" int BIO_printf(BIO *bio, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int ret = ossl.ossl_BIO_vprintf(bio, format, args);
  va_end(args);
  return ret;
}
