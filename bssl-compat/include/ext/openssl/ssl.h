#ifndef __EXT_OPENSSL_SSL_H__
#define __EXT_OPENSSL_SSL_H__

#include <openssl/ssl.h>
#include <ossl/openssl/ssl.h>

#define OSSL_ASYNC_FD ossl_OSSL_ASYNC_FD

OPENSSL_EXPORT int ext_SSL_get_all_async_fds(SSL *s, OSSL_ASYNC_FD *fds, size_t *numfds);

#endif
