#include <openssl/digest.h>
#include <ossl.h>


extern "C" void EVP_MD_CTX_move(EVP_MD_CTX *out, EVP_MD_CTX *in) {
  ossl.ossl_EVP_MD_CTX_copy_ex(out, in);
}
