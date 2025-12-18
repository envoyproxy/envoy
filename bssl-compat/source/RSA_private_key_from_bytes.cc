#include <openssl/rsa.h>
#include <openssl/bio.h>
#include <ossl.h>


/*
 * https://github.com/google/boringssl/blob/cd0b767492199a82c7e362d1a117e8c3fef6b943/include/openssl/rsa.h#L557
 * https://www.openssl.org/docs/man3.0/man3/d2i_RSAPrivateKey_bio.html
 */
extern "C" RSA *RSA_private_key_from_bytes(const uint8_t *in, size_t in_len) {
  bssl::UniquePtr<BIO> bio {ossl.ossl_BIO_new_mem_buf(in, in_len)};
  return ossl.ossl_d2i_RSAPrivateKey_bio(bio.get(), nullptr);
}
