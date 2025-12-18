#include <openssl/stack.h>
#include <ossl.h>


size_t OPENSSL_sk_num(const OPENSSL_STACK *sk) {
  if (sk == NULL) {
    return 0; // OpenSSL returns -1, but BoringSSL returns 0
  }
  return ossl.ossl_OPENSSL_sk_num(sk);
}
