#include <openssl/stack.h>
#include <ossl.h>


void *OPENSSL_sk_last(const OPENSSL_STACK *sk) {
  int num = ossl.ossl_OPENSSL_sk_num(sk);
  if (num <= 0) {
    return NULL; // OpenSSL has no equivalent, and returns -1 for a NULL stack
  }
  return ossl.ossl_OPENSSL_sk_value(sk, num - 1);
}
