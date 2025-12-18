#include <gtest/gtest.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>


TEST(RSATest, test_RSA_set0_factors) {
  bssl::UniquePtr<RSA> key {RSA_new()};
  BIGNUM *p {BN_new()};
  BIGNUM *q {BN_new()};
  ASSERT_EQ(1, RSA_set0_factors(key.get(), p, q));
  const BIGNUM *p2 {};
  const BIGNUM *q2 {};
  RSA_get0_factors(key.get(), &p2, &q2);
  ASSERT_EQ(p, p2);
  ASSERT_EQ(q, q2);
}

TEST(RSATest, test_RSA_set0_key) {
  bssl::UniquePtr<RSA> key {RSA_new()};
  BIGNUM *n {BN_new()};
  BIGNUM *e {BN_new()};
  BIGNUM *d {BN_new()};
  ASSERT_EQ(1, RSA_set0_key(key.get(), n, e, d));
  const BIGNUM *n2 {};
  const BIGNUM *e2 {};
  const BIGNUM *d2 {};
  RSA_get0_key(key.get(), &n2, &e2, &d2);
  ASSERT_EQ(n2, n);
  ASSERT_EQ(e2, e);
  ASSERT_EQ(d2, d);
}

TEST(RSATest, test_RSA_set0_crt_params) {
  bssl::UniquePtr<RSA> key {RSA_new()};
  BIGNUM *dmp1 {BN_new()};
  BIGNUM *dmq1 {BN_new()};
  BIGNUM *iqmp {BN_new()};
  ASSERT_EQ(1, RSA_set0_crt_params(key.get(), dmp1, dmq1, iqmp));
  const BIGNUM *dmp12 {};
  const BIGNUM *dmq12 {};
  const BIGNUM *iqmp2 {};
  RSA_get0_crt_params(key.get(), &dmp12, &dmq12, &iqmp2);
  ASSERT_EQ(dmp12, dmp1);
  ASSERT_EQ(dmq12, dmq1);
  ASSERT_EQ(iqmp2, iqmp);
}