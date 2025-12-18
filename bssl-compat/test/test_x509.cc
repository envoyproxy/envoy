#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "certs/server_1_cert.pem.h"
#include "crypto/test/test_util.h"

TEST(X509Test, test_X509_NAME_digest) {
  bssl::UniquePtr<BIO> bio{BIO_new_mem_buf(server_1_cert_pem_str, strlen(server_1_cert_pem_str))};
  ASSERT_TRUE(bio);
  bssl::UniquePtr<X509> cert{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
  ASSERT_TRUE(cert);
  X509_NAME* sn{X509_get_subject_name(cert.get())};
  ASSERT_TRUE(sn);

  uint8_t buf[EVP_MAX_MD_SIZE];
  unsigned len;

  ASSERT_EQ(1, X509_NAME_digest(sn, EVP_sha256(), buf, &len));
  ASSERT_EQ(32, len);

  uint8_t expected[]{0x19, 0x27, 0x3b, 0xb5, 0x60, 0x9c, 0xa4, 0x45, 0x9e, 0xa8, 0x73,
                     0x0d, 0x7f, 0x5f, 0xb5, 0xf1, 0xd3, 0x5c, 0x06, 0xad, 0x3d, 0x2b,
                     0x94, 0x98, 0x1c, 0x65, 0xb8, 0x76, 0x8d, 0xee, 0x15, 0xed};

  ASSERT_EQ(Bytes(expected), Bytes(buf, len));
}

TEST(X509Test, test_X509_get_X509_PUBKEY) {
  bssl::UniquePtr<BIO> bio{BIO_new_mem_buf(server_1_cert_pem_str, strlen(server_1_cert_pem_str))};
  ASSERT_TRUE(bio);
  bssl::UniquePtr<X509> cert{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
  ASSERT_TRUE(cert);
  X509_PUBKEY* pubkey{X509_get_X509_PUBKEY(cert.get())};
  ASSERT_TRUE(pubkey);
  bssl::UniquePtr<EVP_PKEY> pkey{X509_PUBKEY_get(pubkey)};
  ASSERT_TRUE(pkey);
  ASSERT_EQ(EVP_PKEY_RSA, EVP_PKEY_id(pkey.get()));
}

TEST(X509Test, test_X509_digest) {
  bssl::UniquePtr<BIO> bio{BIO_new_mem_buf(server_1_cert_pem_str, strlen(server_1_cert_pem_str))};
  ASSERT_TRUE(bio);
  bssl::UniquePtr<X509> cert{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
  ASSERT_TRUE(cert);

  uint8_t buf[EVP_MAX_MD_SIZE];
  unsigned len;

  ASSERT_EQ(1, X509_digest(cert.get(), EVP_sha256(), buf, &len));
  ASSERT_EQ(32, len);

  uint8_t expected[]{0xc3, 0x49, 0x59, 0xa8, 0x3b, 0x10, 0xa2, 0xef, 0x91, 0xe5, 0x30,
                     0x3f, 0x63, 0xdb, 0xbd, 0x1b, 0xfd, 0x63, 0xc7, 0xf6, 0x6a, 0x75,
                     0xbb, 0x9f, 0x43, 0x4a, 0x77, 0xad, 0x9d, 0x9a, 0xfc, 0x09};

  ASSERT_EQ(Bytes(expected), Bytes(buf, len));
}

TEST(X509Test, test_i2d_X509_PUBKEY) {
  bssl::UniquePtr<BIO> bio{BIO_new_mem_buf(server_1_cert_pem_str, strlen(server_1_cert_pem_str))};
  ASSERT_TRUE(bio);

  bssl::UniquePtr<X509> cert{PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr)};
  ASSERT_TRUE(cert);

  X509_PUBKEY* pubkey{X509_get_X509_PUBKEY(cert.get())};
  ASSERT_TRUE(pubkey);

  uint8_t* bytes{nullptr};
  const int len = i2d_X509_PUBKEY(pubkey, &bytes);
  ASSERT_TRUE(bytes);
  ASSERT_EQ(len, 294);

  OPENSSL_free(bytes);
}
