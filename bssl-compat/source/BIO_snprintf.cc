#include <openssl/bio.h>
#include <ossl.h>


extern "C" int BIO_snprintf(char *buf, size_t n, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int ret = ossl.ossl_BIO_vsnprintf(buf, n, format, args);
  va_end(args);
  return ret;
}
