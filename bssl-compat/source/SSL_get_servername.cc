#include <openssl/ssl.h>
#include "SSL_CTX_set_select_certificate_cb.h"


static const unsigned char* extract_ext_str_value(const unsigned char *data, size_t& data_len, unsigned int type) {
    // https://github.com/openssl/openssl/blob/12a765a5235f181c2f4992b615eb5f892c368e88/test/handshake_helper.c#L150
    unsigned int len, remaining = data_len;
    const unsigned char *p = data;
    if ( p && remaining > 2 ) {
        // ntohs?
        len = (*(p++) << 8);
        len += *(p++);
        if (len + 2 == remaining) {
            remaining = len;
            if (remaining > 0 && *p++ == type) {
                remaining--;
                /* Now we can finally pull out the byte array with the actual extn value. */
                if (remaining > 2) {
                    len = (*(p++) << 8);
                    len += *(p++);
                    if (len + 2 == remaining) {
                        data_len = len;
                        return p;
                    }
                }
            }
        }
    }

    data_len = 0;
    return nullptr;
}

/*
 * This function effectively has two separate implementations, chosen depending
 * on the invocation context.
 * 
 * If invoked from within a SSL_CTX_set_select_certificate_cb() callback
 * function (as indincated by the in_select_certificate_cb(ssl) query) then the
 * OpenSSL implementation doesn't work for us. Instead we have to extract the
 * servername host name bytes from the appropriate client hello extension. In
 * all other cases we simply call the OpenSSL implementation.
 */
const char *SSL_get_servername(const SSL *ssl, const int type) {
  if (in_select_certificate_cb(ssl)) {
    // Allocate an ext data index for us to use, and plug in a free func
    static int index {SSL_get_ex_new_index(0, nullptr, nullptr, nullptr,
                        +[](void *, void *ptr, CRYPTO_EX_DATA *, int, long, void*) {
                        if (ptr) ossl_OPENSSL_free(ptr);
                        })};

    const unsigned char* p;
    size_t len;

    // Extract the bytes from the client hello SNI extension, if present.
    if (ossl_SSL_client_hello_get0_ext(const_cast<SSL*>(ssl), type, &p, &len)) {
        if ((p = extract_ext_str_value(p, len, type)) != nullptr) {
        // The string pointed to by p is len bytes long but NOT null-terminated.
        // Therefore, we have to make a null-terminated copy of it for returning.
        char *copy {ossl_OPENSSL_strndup(reinterpret_cast<const char*>(p), len)};

        // The free func (registered with SSL_get_ex_new_index() above) is only
        // called when the SSL object is finally freed. Therefore, we need to
        // explicitly free any ex data value that may in the slot from previous
        // calls on the same SSL object.
        ossl_OPENSSL_free(SSL_get_ex_data(ssl, index));

        // Squirel away the copy in the SSL object's ext data so it won't leak.
        if (SSL_set_ex_data(const_cast<SSL*>(ssl), index, copy) == 0) {
            ossl_OPENSSL_free(copy);
            return nullptr;
        }

        return copy;
        }
    }

    return nullptr;
  }
  else {
    return ossl_SSL_get_servername(ssl, type);
  }
}
