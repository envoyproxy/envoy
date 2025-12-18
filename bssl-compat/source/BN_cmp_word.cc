#include <openssl/bn.h>
#include <ossl.h>


extern "C" int BN_cmp_word(const BIGNUM *a, BN_ULONG b) {
  bssl::UniquePtr<BIGNUM> b_bn {BN_new()};
  ossl.ossl_BN_set_word(b_bn.get(), b);

  return ossl.ossl_BN_cmp(a, b_bn.get());
}
