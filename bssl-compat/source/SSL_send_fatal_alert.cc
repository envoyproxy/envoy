#include <openssl/ssl.h>
#include "log.h"


extern "C" int SSL_send_fatal_alert(SSL *ssl, uint8_t alert) {
  bssl_compat_fatal("SSL_send_fatal_alert() is not implemented");
  return -1;
}
