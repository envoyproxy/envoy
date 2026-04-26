#include <openssl/ssl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L2349
 *
 * SSL_get_group_name returns a human-readable name for the group
 * specified by the given TLS group ID. This is the same as SSL_get_curve_name.
 */
const char *SSL_get_group_name(uint16_t group_id) {
  return SSL_get_curve_name(group_id);
}
