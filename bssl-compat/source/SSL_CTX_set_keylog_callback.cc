#include <openssl/ssl.h>
#include <ossl.h>

void SSL_CTX_set_keylog_callback( SSL_CTX *ctx, void (*cb)(const SSL *ssl, const char *line)) {
#ifdef ossl_SSL_CTX_set_keylog_callback
    return ossl_SSL_CTX_set_keylog_callback(ctxa, cb);
#else
    return ossl.ossl_SSL_CTX_set_keylog_callback(ctx, cb);
#endif
}