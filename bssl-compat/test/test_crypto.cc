#include <gtest/gtest.h>
#include <openssl/crypto.h>

TEST(TestCrypto, test_FIPS_mode) {
  ASSERT_EQ(0, FIPS_mode());
}

