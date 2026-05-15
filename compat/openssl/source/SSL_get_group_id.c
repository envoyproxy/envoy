#include <openssl/ssl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L2342
 *
 * SSL_get_group_id returns the ID of the group used by the current TLS
 * connection. This is the same as SSL_get_curve_id.
 */
uint16_t SSL_get_group_id(const SSL *ssl) {
  return SSL_get_curve_id(ssl);
}
