#include <openssl/ssl.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#LL3882C20-L3882C40
 * https://www.openssl.org/docs/man3.0/man3/SSL_get_ex_new_index.html
 * 
 * Out of the three callback function pointers, BoringSSL only makes use of the
 * free_func. The new & dup callbacks are completely ignored, and therefore
 * never called, even if a real function pointer is passed, which is why we
 * pass them as nullptr to OpenSSL.
 */
extern "C" int SSL_get_ex_new_index(long argl, void *argp, CRYPTO_EX_unused *new_func, CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func) {
  return ossl.ossl_SSL_get_ex_new_index(argl, argp, nullptr, nullptr, free_func);
}
