#include <openssl/digest.h>
#include <ossl.h>

extern "C" void EVP_MD_CTX_init(EVP_MD_CTX* ctx) { ossl.ossl_EVP_MD_CTX_init(ctx); }
