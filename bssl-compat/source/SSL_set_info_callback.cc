#include <openssl/ssl.h>
#include <ossl.h>


void SSL_set_info_callback( SSL *ssl, void (*cb)(const SSL *ssl, int type, int value)) {
#ifdef ossl_SSL_set_info_callback
    return ossl_SSL_set_info_callback(ssl, ssl, cb);
#else
    return ossl.ossl_SSL_set_info_callback(ssl, cb);
#endif
}
