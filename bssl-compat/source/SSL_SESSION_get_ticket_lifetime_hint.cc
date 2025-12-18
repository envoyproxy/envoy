#include <openssl/ssl.h>
#include <ossl.h>


extern "C" uint32_t SSL_SESSION_get_ticket_lifetime_hint(const SSL_SESSION *session) {
  return ossl.ossl_SSL_SESSION_get_ticket_lifetime_hint(session);
}