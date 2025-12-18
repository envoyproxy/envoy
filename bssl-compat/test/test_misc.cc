#include <gtest/gtest.h>
#include <openssl/asn1.h>
#include <openssl/ec_key.h>
#include <openssl/digest.h>
#include <openssl/ecdsa.h>


TEST(MiscTest, test_UniquePtr_ASN1_OBJECT) {
  bssl::UniquePtr<ASN1_OBJECT> p;
}

TEST(MiscTest, test_UniquePtr_EC_KEY) {
  bssl::UniquePtr<EC_KEY> p;
}

TEST(MiscTest, test_UniquePtr_EVP_MD_CTX) {
  bssl::UniquePtr<EVP_MD_CTX> p;
}

TEST(MiscTest, test_UniquePtr_ECDSA_SIG) {
  bssl::UniquePtr<ECDSA_SIG> p;
}

