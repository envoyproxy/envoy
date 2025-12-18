#include <openssl/ssl.h>
#include "log.h"


extern "C" void SSL_CTX_set_private_key_method(SSL_CTX *ctx, const SSL_PRIVATE_KEY_METHOD *key_method) {
  bssl_compat_fatal("SSL_CTX_set_private_key_method() is not implemented");
}
