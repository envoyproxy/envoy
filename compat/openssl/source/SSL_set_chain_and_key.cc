#include <openssl/ssl.h>
#include <ossl.h>

#include "CRYPTO_BUFFER.h"
#include "log.h"

/*
 * https://github.com/google/boringssl/blob/098695591f3a2665fccef83a3732ecfc99acdcdd/src/include/openssl/ssl.h#L1133
 * https://www.openssl.org/docs/man3.0/man3/SSL_use_cert_and_key.html
 */
extern "C" int SSL_set_chain_and_key(SSL* ssl, CRYPTO_BUFFER* const* certs, size_t num_certs,
                                     EVP_PKEY* privkey,
                                     const SSL_PRIVATE_KEY_METHOD* privkey_method) {
  if (privkey_method) {
    bssl_compat_fatal("%s() : privkey_method parameter is not supported");
  }

  bssl::UniquePtr<X509> leaf;
  bssl::UniquePtr<STACK_OF(X509)> chain{sk_X509_new_null()};

  for (size_t i = 0; i < num_certs; i++) {
    bssl::UniquePtr<BIO> bio{BIO_new_mem_buf(certs[i]->data, certs[i]->len)};
    if (!bio) {
      return 0;
    }

    bssl::UniquePtr<X509> cert{ossl.ossl_d2i_X509_bio(bio.get(), nullptr)};
    if (!cert) {
      return 0;
    }

    if (i == 0) {
      leaf = std::move(cert);
    } else if (sk_X509_push(chain.get(), cert.get()) == i) {
      cert.release();
    } else {
      return 0;
    }
  }

  return ossl.ossl_SSL_use_cert_and_key(
      ssl, leaf.get(), privkey, reinterpret_cast<ossl_STACK_OF(ossl_X509)*>(chain.get()), 1);
}
