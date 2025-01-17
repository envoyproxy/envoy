#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/crypto_impl.h"
#include "source/common/crypto/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Common {
namespace Crypto {
namespace {

TEST(UtilityTest, TestSha256Digest) {
  const Buffer::OwnedImpl buffer("test data");
  const auto digest = UtilitySingleton::get().getSha256Digest(buffer);
  EXPECT_EQ("916f0027a575074ce72a331777c3478d6513f786a591bd892da1a577bf2335f9",
            Hex::encode(digest));
}

TEST(UtilityTest, TestSha256DigestWithEmptyBuffer) {
  const Buffer::OwnedImpl buffer;
  const auto digest = UtilitySingleton::get().getSha256Digest(buffer);
  EXPECT_EQ("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            Hex::encode(digest));
}

TEST(UtilityTest, TestSha256DigestGrowingBuffer) {
  // Adding multiple slices to the buffer
  Buffer::OwnedImpl buffer("slice 1");
  auto digest = UtilitySingleton::get().getSha256Digest(buffer);
  EXPECT_EQ("76571770bb46bdf51e1aba95b23c681fda27f6ae56a8a90898a4cb7556e19dcb",
            Hex::encode(digest));
  buffer.add("slice 2");
  digest = UtilitySingleton::get().getSha256Digest(buffer);
  EXPECT_EQ("290b462b0fe5edcf6b8532de3ca70da8ab77937212042bb959192ec6c9f95b9a",
            Hex::encode(digest));
  buffer.add("slice 3");
  digest = UtilitySingleton::get().getSha256Digest(buffer);
  EXPECT_EQ("29606bbf02fdc40007cdf799de36d931e3587dafc086937efd6599a4ea9397aa",
            Hex::encode(digest));
}

TEST(UtilityTest, TestSha256Hmac) {
  const std::string key = "key";
  auto hmac = UtilitySingleton::get().getSha256Hmac(std::vector<uint8_t>(key.begin(), key.end()),
                                                    "test data");
  EXPECT_EQ("087d9eb992628854842ca4dbf790f8164c80355c1e78b72789d830334927a84c", Hex::encode(hmac));
}

TEST(UtilityTest, TestSha256HmacWithEmptyArguments) {
  auto hmac = UtilitySingleton::get().getSha256Hmac(std::vector<uint8_t>(), "");
  EXPECT_EQ("b613679a0814d9ec772f95d778c35fc5ff1697c493715653c6c712144292c5ad", Hex::encode(hmac));
}

TEST(UtilityTest, TestImportPublicKey) {
  auto key = "30820122300d06092a864886f70d01010105000382010f003082010a0282010100a7471266d01d160308d"
             "73409c06f2e8d35c531c458d3e480e9f3191847d062ec5ccff7bc51e949d5f2c3540c189a4eca1e8633a6"
             "2cf2d0923101c27e38013e71de9ae91a704849bff7fbe2ce5bf4bd666fd9731102a53193fe5a9a5a50644"
             "ff8b1183fa897646598caad22a37f9544510836372b44c58c98586fb7144629cd8c9479592d996d32ff6d"
             "395c0b8442ec5aa1ef8051529ea0e375883cefc72c04e360b4ef8f5760650589ca814918f678eee39b884"
             "d5af8136a9630a6cc0cde157dc8e00f39540628d5f335b2c36c54c7c8bc3738a6b21acff815405afa28e5"
             "183f550dac19abcf1145a7f9ced987db680e4a229cac75dee347ec9ebce1fc3dbbbb0203010001";

  Common::Crypto::CryptoObjectPtr crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPublicKey(Hex::decode(key)));
  auto wrapper = Common::Crypto::Access::getTyped<Common::Crypto::PublicKeyObject>(*crypto_ptr);
  EVP_PKEY* pkey = wrapper->getEVP_PKEY();
  EXPECT_NE(nullptr, pkey);

  key = "badkey";
  crypto_ptr = Common::Crypto::UtilitySingleton::get().importPublicKey(Hex::decode(key));
  wrapper = Common::Crypto::Access::getTyped<Common::Crypto::PublicKeyObject>(*crypto_ptr);
  pkey = wrapper->getEVP_PKEY();
  EXPECT_EQ(nullptr, pkey);

  EVP_PKEY* empty_pkey = EVP_PKEY_new();
  wrapper->setEVP_PKEY(empty_pkey);
  pkey = wrapper->getEVP_PKEY();
  EXPECT_NE(nullptr, pkey);
}

TEST(UtilityTest, TestVerifySignature) {
  auto key = "30820122300d06092a864886f70d01010105000382010f003082010a0282010100ba10ebe185465586093"
             "228fb3b0093c560853b7ebf28497aefb9961a6cc886dd3f6d3278a93244fa5084a9c263bd57feb4ea1868"
             "aa8a2718aa46708c803ce49318619982ba06a6615d24bb853c0fb85ebed833a802245e4518d4e2ba10da1"
             "f22c732505433c558bed8895eb1e97cb5d65f821be9330143e93a738ef6896165879f692d75c2d7928e01"
             "fd7fe601d16931bdd876c7b15b741e48546fe80db45df56e22ed2fa974ab937af7644d20834f41a61aeb9"
             "a70d0248d274642b14ed6585892403bed8e03a9a12485ae44e3d39ab53e5bd70dee58476fb81860a18679"
             "9429b71f79f204894cf21d31cc19118d547bb1b946532d080e074ec97e23667818490203010001";
  auto data = "hello\n";

  Common::Crypto::CryptoObjectPtr crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPublicKey(Hex::decode(key)));
  Common::Crypto::CryptoObject* crypto(crypto_ptr.get());

  std::vector<uint8_t> text(data, data + strlen(data));

  // Map of hash function names and their respective signatures
  std::map<std::string, std::string> hashSignatures = {
      {"sha1",
       "9ed4cc60e8b2b51ff00b1cc06c628263476c8be6136510fc47e4668423c3492d8711489000b32163cd022661049"
       "360fa0b8366692e41a4d4fb4237479694484012833ccc88938c1a471e33757c198b42623961d91cdf41ca01b375"
       "002930b37a62377517cad297d5658e31610acaf79216a3d5c0afe4715dfe6e5bad3c20dac77bbbd2e7a4cb44172"
       "abb620fe60b968426726ed25d2aed99abf9e8f705b7021e3448a78824e6982e9d14dbd0a317f45d42198f785f3b"
       "0ca8e311695cedb4ce19626c246b8010a5de7b7978b8a3b56c1558f87bd658023e52b6e155c39594bae6e3cbf77"
       "9d487a9ce3bffd7d8a2641f336771bec5c9d4a40dc8d4163fd2c1dd3b"}, // Signature for SHA-1 hash
                                                                     // function
      {"sha224",
       "03813d50082dfb43444ebf47788e69271ebbfa17e64f7e7600ce761bd89ff459e21ecea6bc7de8396cfd80fe0ee"
       "3d92967f0c467c930f7d0b1b1734e5d139ffaa5d84c5047cb38793b152ba8b284ec6d31e0b410b1e1a06ffda171"
       "42c83b30593ac02a2f07f8e863ade752d23b2f41d56bd1ab6328c46de47233e2e2e4189e5bd3bce0b0428f485ff"
       "e75f7343d89b376bd7dc2953467e63f5c1eb9279ca349fa74535d37e80f57216b8b73b0e67b32f0f18f41bae6a7"
       "6e350dbc6188525eda1c79c0977bf433bb170d49de47985bc14a418d7a03d72eda740666dc05185fdcea6bb2914"
       "d7bd0271bd06b3de72bc9db82d625799bf3441e2abff8fcd273efe6c7"}, // Signature for SHA-224 hash
                                                                     // function
      {"sha256",
       "504c24addc615c01c915e3244b8248b470a41c82faa6a82526ddd8f21e197adae6c8b985e40960157d47f336d8b"
       "a31ce4b3b1795379a7415af57daa757d3027e870b3c6644e749b583c51a16f9984afd39c909325d271d8d4c8d00"
       "6288bd8f7945aa24a783613ecece95a9692b3b56dd1d831fc06d82eca40fd432a15a6cdb837d7ce378ac889c4ab"
       "00b0c1f9c2be279952696b70c9ea2bb014d6f20b72ed4917904d5f24d5776058bd11121f3ed02e03c384cf42734"
       "6b1d300867969f22e27aa9f0607344cc9d8e9a90802e97ac39af9324f60ddad24682e48424346dd5a53fe550370"
       "bdf9f94dec8a80810edd648a33cc767f9e4328660e3ee1be8b47e9cfa"}, // Signature for SHA-256 hash
                                                                     // function
      {"sha384",
       "91491000964d11f12ff658101f7c21ce8b37da6fff3abe836859c17e268858d77ee9c96e88ca86b36bca2b06472"
       "a1f551d642bf056f1241d11d5b853e1458c2a9d86f9096e872c81480a06000346a61e51cb94e5174a98b9daacf5"
       "204dd28e081c99a72066c406334a046ae5f3eb0e0eea86f0ae7eeb27d5dea245e850d05cc6c972f8249b8a4f018"
       "6531735137a2e45f1f6410bf8e2382e95b57618802a0068ca197b2d8bcca53d6738e04b86ed9c69d45dad6d9bd7"
       "be55596a719f12531d363e74c9d659738eaa50ab854869416f2b445f054aa2c1223c9edd223cbc5ac0d3582cb9b"
       "5af494138bd6ace049e3ab326bb23fadd3dbcd74e9a3b372843f926ec"}, // Signature for SHA-384 hash
                                                                     // function
      {"sha512",
       "5d001462d000c0aa23d931f6cce5def5f8472c7aaa0185cab87b256697b7a0c8fb6a4c9f84debf1b4ff3bf53213"
       "0bcb25f724e09a74b5d5c915feb9c943a005ab879078b2fbcab0828e128ebfb7befee25d219bcd6cf1ad1f62b94"
       "b460021eebc4c249e34219c71b4f526628976ecea8fb70e1166053da212747e8ba4b29cb91fa6541d53d3400a9d"
       "34881a227e01eebf157104d84555c9e20320280723a72d3a724eba99f1fb14d59399321636ebfe7070d83d7b6b2"
       "381fcdb683fb73e7796d36fe45dfb14a622c3426fe5bf69af9c24f9f1b30affad129b5f2b7dfa6fa384c73ad212"
       "f414606882c3f9133d4702f487f9b08df8d0265fe5e8e12a11c6cb35c"}, // Signature for SHA-512 hash
                                                                     // function
  };

  // Loop through each hash function and its signature
  for (const auto& entry : hashSignatures) {
    const std::string& hash_func = entry.first;
    const std::string& signature = entry.second;
    auto sig = Hex::decode(signature);

    auto result = UtilitySingleton::get().verifySignature(hash_func, *crypto, sig, text);
    EXPECT_EQ(true, result.result_);
    EXPECT_EQ("", result.error_message_);
  }

  auto signature =
      "504c24addc615c01c915e3244b8248b470a41c82faa6a82526ddd8f21e197adae6c8b985e40960157d47f336d8ba"
      "31ce4b3b1795379a7415af57daa757d3027e870b3c6644e749b583c51a16f9984afd39c909325d271d8d4c8d0062"
      "88bd8f7945aa24a783613ecece95a9692b3b56dd1d831fc06d82eca40fd432a15a6cdb837d7ce378ac889c4ab00b"
      "0c1f9c2be279952696b70c9ea2bb014d6f20b72ed4917904d5f24d5776058bd11121f3ed02e03c384cf427346b1d"
      "300867969f22e27aa9f0607344cc9d8e9a90802e97ac39af9324f60ddad24682e48424346dd5a53fe550370bdf9f"
      "94dec8a80810edd648a33cc767f9e4328660e3ee1be8b47e9cfa";
  auto sig = Hex::decode(signature);

  // Test an unknown hash function
  auto result = UtilitySingleton::get().verifySignature("unknown", *crypto, sig, text);
  EXPECT_EQ(false, result.result_);
  EXPECT_EQ("unknown is not supported.", result.error_message_);

  // Test with an empty crypto object
  auto empty_crypto = std::make_unique<PublicKeyObject>();
  result = UtilitySingleton::get().verifySignature("sha256", *empty_crypto, sig, text);
  EXPECT_EQ(false, result.result_);
  EXPECT_EQ("Failed to initialize digest verify.", result.error_message_);

  // Test with incorrect data
  data = "baddata";
  text = std::vector<uint8_t>(data, data + strlen(data));
  result = UtilitySingleton::get().verifySignature("sha256", *crypto, sig, text);
  EXPECT_EQ(false, result.result_);
  EXPECT_EQ("Failed to verify digest. Error code: 0", result.error_message_);

  // Test with incorrect signature
  data = "hello";
  text = std::vector<uint8_t>(data, data + strlen(data));
  result = UtilitySingleton::get().verifySignature("sha256", *crypto, Hex::decode("000000"), text);
  EXPECT_EQ(false, result.result_);
  EXPECT_EQ("Failed to verify digest. Error code: 0", result.error_message_);
}

} // namespace
} // namespace Crypto
} // namespace Common
} // namespace Envoy
