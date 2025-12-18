#include <openssl/ssl.h>
#include <ossl.h>


extern "C" int SSL_CTX_get_session_cache_mode(const SSL_CTX *ctx) {
  return ossl_SSL_CTX_get_session_cache_mode(const_cast<SSL_CTX*>(ctx));
}
