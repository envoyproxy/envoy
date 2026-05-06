#include <openssl/ssl.h>

extern "C" int SSL_ech_accepted(const SSL* ssl) {
  // ECH has no equivalent in OpenSSL; ECH is never accepted.
  (void)ssl;
  return 0;
}
