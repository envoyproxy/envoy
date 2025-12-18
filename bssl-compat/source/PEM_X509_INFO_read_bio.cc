#include <openssl/pem.h>
#include <ossl.h>
#include "log.h"


/*
 * https://github.com/google/boringssl/blob/b9ec9dee569854ac3dee909b9dfe8c1909a6c751/include/openssl/pem.h#L350
 * https://www.openssl.org/docs/man3.0/man3/PEM_X509_INFO_read_bio.html
 * 
 * Note that the BoringSSL and OpenSSL versions of PEM_X509_INFO_read_bio() have
 * slightly different behaviour in the case where an error occurs *and* a non-null
 * |sk| value was passed in.
 */
extern "C" STACK_OF(X509_INFO) *PEM_X509_INFO_read_bio(BIO *bp, STACK_OF(X509_INFO) *sk, pem_password_cb *cb, void *u) {
  STACK_OF(X509_INFO) *saved {sk};

  auto ret {reinterpret_cast<STACK_OF(X509_INFO)*>(ossl.ossl_PEM_X509_INFO_read_bio(bp, nullptr, cb, u))};

  if ((ret != nullptr) && (saved != nullptr)) {
    for (size_t i = 0, max = sk_X509_INFO_num(ret); i < max; i++) {
      sk_X509_INFO_push(saved, sk_X509_INFO_value(ret, i));
    }
    sk_X509_INFO_free(ret);
    ret = saved;
  }

  return ret;
}
