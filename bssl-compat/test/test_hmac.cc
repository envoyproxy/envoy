#include <gtest/gtest.h>
#include <openssl/hmac.h>


TEST(HMACTest, test_HMAC) {
  std::string key {"hello"};
  std::string data {"plain text data"};
  std::string digest {"27a8d47ded4d53080d3582dbb396ba74"};

  uint8_t out[EVP_MAX_MD_SIZE];
  unsigned int out_len;
  uint8_t *hmac = HMAC(EVP_md5(), key.data(), key.length(), (uint8_t*)data.data(), data.length(), out, &out_len);
  ASSERT_TRUE(hmac);
  ASSERT_EQ(digest.length() / 2, out_len);

  char hex[EVP_MAX_MD_SIZE * 2];
  for (int i = 0; i < out_len; i++) {
    sprintf(&(hex[i * 2]), "%02x", hmac[i]);
  }
  hex[out_len * 2] = '\0';

  EXPECT_EQ(digest, hex);
}
