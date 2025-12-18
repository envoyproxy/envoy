#ifndef _SSL_CTX_SET_SELECT_CERTIFICATE_CB_H_
#define _SSL_CTX_SET_SELECT_CERTIFICATE_CB_H_

#include <openssl/ssl.h>


/*
 * Returns true if called from within a SSL_CTX_set_select_certificate_cb()
 * callback that is currently being invoked for the specified SSL object.
 */
bool in_select_certificate_cb(const SSL *ssl);

#endif /*_SSL_CTX_SET_SELECT_CERTIFICATE_CB_H_*/
