#include <openssl/ssl.h>
#include <ossl.h>

extern "C" uint16_t SSL_get_max_proto_version(const SSL* ssl) {
  return ossl.ossl_SSL_get_max_proto_version((SSL*)ssl);
}
