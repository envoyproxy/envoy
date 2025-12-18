#include <openssl/ssl.h>
#include <ossl.h>

extern "C" {
int SSL_CTX_get_ex_new_index(long argl, void *argp,
                             CRYPTO_EX_unused *unused,
                             CRYPTO_EX_dup *dup_unused,
                             CRYPTO_EX_free *free_func) {

    /**
     * OpenSSL has the following macro definition
     * #define SSL_CTX_get_ex_new_index(l, p, newf, dupf, freef) \
     *         CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_SSL_CTX, l, p, newf, dupf, freef)
     *
     * https://www.openssl.org/docs/man3.0/man3/SSL_CTX_get_ex_new_index.html
     */
    return ossl.ossl_CRYPTO_get_ex_new_index(ossl_CRYPTO_EX_INDEX_SSL_CTX,argl, argp,
                                             reinterpret_cast<ossl_CRYPTO_EX_new *>(unused),
                                             dup_unused,
                                              free_func );
}
}