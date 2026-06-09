#include <openssl/ssl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L2355
 *
 * SSL_get_all_group_names outputs a list of all group names. This is the same
 * as SSL_get_all_curve_names.
 */
size_t SSL_get_all_group_names(const char **out, size_t max_out) {
  return SSL_get_all_curve_names(out, max_out);
}
