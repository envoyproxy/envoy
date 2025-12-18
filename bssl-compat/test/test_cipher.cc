#include <gtest/gtest.h>
#include <openssl/cipher.h>
#include <openssl/rand.h>


TEST(CipherTest, cipher1) {
  const EVP_CIPHER *cipher = EVP_aes_256_cbc();

  ASSERT_TRUE(cipher);

  std::vector<uint8_t> key(EVP_CIPHER_key_length(cipher));
  std::vector<uint8_t> iv(EVP_CIPHER_iv_length(cipher));
  
  ASSERT_EQ(1, RAND_bytes(key.data(), key.size()));
  ASSERT_EQ(1, RAND_bytes(iv.data(), iv.size()));

  std::vector<uint8_t> plaintext1 { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
  std::vector<uint8_t> ciphertext;
  std::vector<uint8_t> plaintext2;

  {
    int l1, l2;
    bssl::UniquePtr<EVP_CIPHER_CTX> ctx(EVP_CIPHER_CTX_new());

    ciphertext.resize(plaintext1.size() + EVP_CIPHER_block_size(cipher));
    ASSERT_EQ(1, EVP_EncryptInit_ex(ctx.get(), cipher, nullptr, key.data(), iv.data()));
    ASSERT_EQ(1, EVP_EncryptUpdate(ctx.get(), ciphertext.data(), &l1, plaintext1.data(), plaintext1.size()));
    ASSERT_EQ(1, EVP_EncryptFinal_ex(ctx.get(), ciphertext.data() + l1, &l2));
    ciphertext.resize(l1 + l2); // Resize to the actual encrypted byte count
  }

  {
    int l1, l2;
    bssl::UniquePtr<EVP_CIPHER_CTX> ctx(EVP_CIPHER_CTX_new());

    plaintext2.resize(plaintext1.size() + EVP_CIPHER_block_size(cipher));
    ASSERT_EQ(1, EVP_DecryptInit_ex(ctx.get(), cipher, nullptr, key.data(), iv.data()));
    ASSERT_EQ(1, EVP_DecryptUpdate(ctx.get(), plaintext2.data(), &l1, ciphertext.data(), ciphertext.size()));
    ASSERT_EQ(1, EVP_DecryptFinal_ex(ctx.get(), plaintext2.data() + l1, &l2));
    plaintext2.resize(l1 + l2); // Resize to the actual decrypted byte count
  }

  ASSERT_EQ(plaintext1, plaintext2);
}