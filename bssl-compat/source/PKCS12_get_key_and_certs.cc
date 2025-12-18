#include <openssl/pkcs8.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/225e8d39b50757af56e61cd0aa7958c56c487d54/include/openssl/pkcs8.h#L127
 * https://www.openssl.org/docs/man3.0/man3/PKCS12_get_key_and_certs.html
 */
extern "C" int PKCS12_get_key_and_certs(EVP_PKEY **out_key, STACK_OF(X509) *out_certs, CBS *in, const char *password) {
  bssl::UniquePtr<BIO> bio {BIO_new_mem_buf(CBS_data(in), CBS_len(in))};
  if (!bio) {
    return 0;
  }

  bssl::UniquePtr<PKCS12> p12 {d2i_PKCS12_bio(bio.get(), nullptr)};
  if (!p12) {
    return 0;
  }

  X509 *tmp_cert;

  if (!PKCS12_parse(p12.get(), password, out_key, &tmp_cert, &out_certs)) {
    return 0;
  }

  if (tmp_cert) {
    if (sk_X509_push(out_certs, tmp_cert) == 0) {
      X509_free(tmp_cert);
      return 0;
    }
  }

  return 1;
}
