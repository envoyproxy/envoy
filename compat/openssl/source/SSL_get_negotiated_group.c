#include <openssl/ssl.h>
#include <ossl.h>


/*
 * BoringSSL declares this with a `const SSL *`, but OpenSSL's
 * ossl_SSL_get_negotiated_group() is a macro that expands to ossl_SSL_ctrl(),
 * which takes a non-const `ossl_SSL *`. Cast away const to avoid the
 * -Wincompatible-pointer-types-discards-qualifiers warning.
 */
int SSL_get_negotiated_group(const SSL *ssl) {
  return ossl.ossl_SSL_get_negotiated_group((SSL*)ssl);
}
