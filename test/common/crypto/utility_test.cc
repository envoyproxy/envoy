#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/hex.h"
#include "source/common/crypto/crypto_impl.h"
#include "source/common/crypto/utility.h"
#include "source/common/crypto/utility_impl.h"

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
  // Test DER format (backward compatibility)
  auto der_key =
      "30820122300d06092a864886f70d01010105000382010f003082010a0282010100a7471266d01d160308d"
      "73409c06f2e8d35c531c458d3e480e9f3191847d062ec5ccff7bc51e949d5f2c3540c189a4eca1e8633a6"
      "2cf2d0923101c27e38013e71de9ae91a704849bff7fbe2ce5bf4bd666fd9731102a53193fe5a9a5a50644"
      "ff8b1183fa897646598caad22a37f9544510836372b44c58c98586fb7144629cd8c9479592d996d32ff6d"
      "395c0b8442ec5aa1ef8051529ea0e375883cefc72c04e360b4ef8f5760650589ca814918f678eee39b884"
      "d5af8136a9630a6cc0cde157dc8e00f39540628d5f335b2c36c54c7c8bc3738a6b21acff815405afa28e5"
      "183f550dac19abcf1145a7f9ced987db680e4a229cac75dee347ec9ebce1fc3dbbbb0203010001";

  Common::Crypto::PKeyObjectPtr der_crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPublicKeyDER(Hex::decode(der_key)));
  EVP_PKEY* der_pkey = der_crypto_ptr->getEVP_PKEY();
  EXPECT_NE(nullptr, der_pkey) << "DER public key import failed";

  // Test PEM format - same key material as DER
  std::string pem_key = "-----BEGIN PUBLIC KEY-----\n"
                        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAp0cSZtAdFgMI1zQJwG8u\n"
                        "jTXFMcRY0+SA6fMZGEfQYuxcz/e8UelJ1fLDVAwYmk7KHoYzpizy0JIxAcJ+OAE+\n"
                        "cd6a6RpwSEm/9/vizlv0vWZv2XMRAqUxk/5amlpQZE/4sRg/qJdkZZjKrSKjf5VE\n"
                        "UQg2NytExYyYWG+3FEYpzYyUeVktmW0y/205XAuEQuxaoe+AUVKeoON1iDzvxywE\n"
                        "42C0749XYGUFicqBSRj2eO7jm4hNWvgTapYwpswM3hV9yOAPOVQGKNXzNbLDbFTH\n"
                        "yLw3OKayGs/4FUBa+ijlGD9VDawZq88RRaf5ztmH22gOSiKcrHXe40fsnrzh/D27\n"
                        "uwIDAQAB\n"
                        "-----END PUBLIC KEY-----";

  Common::Crypto::PKeyObjectPtr pem_crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPublicKeyPEM(pem_key));
  EVP_PKEY* pem_pkey = pem_crypto_ptr->getEVP_PKEY();
  EXPECT_NE(nullptr, pem_pkey) << "PEM public key import failed";

  // Verify both keys are functionally equivalent
  EXPECT_EQ(EVP_PKEY_bits(der_pkey), EVP_PKEY_bits(pem_pkey))
      << "DER and PEM keys should have same bit size";

  // Test error handling with invalid key
  std::vector<uint8_t> bad_key = {0x1, 0x2, 0x3, 0x4, 0x5};
  auto bad_crypto_ptr = Common::Crypto::UtilitySingleton::get().importPublicKeyDER(bad_key);
  EVP_PKEY* bad_pkey = bad_crypto_ptr->getEVP_PKEY();
  EXPECT_EQ(nullptr, bad_pkey) << "Invalid key should return nullptr";

  // Test setting a valid key on empty object
  EVP_PKEY* empty_pkey = EVP_PKEY_new();
  der_crypto_ptr->setEVP_PKEY(empty_pkey);
  EVP_PKEY* set_pkey = der_crypto_ptr->getEVP_PKEY();
  EXPECT_NE(nullptr, set_pkey) << "Setting valid EVP_PKEY should succeed";
}

TEST(UtilityTest, TestVerifySignature) {
  // Test with both DER and PEM public key formats using pre-computed signatures
  auto der_key =
      "30820122300d06092a864886f70d01010105000382010f003082010a0282010100ba10ebe185465586093"
      "228fb3b0093c560853b7ebf28497aefb9961a6cc886dd3f6d3278a93244fa5084a9c263bd57feb4ea1868"
      "aa8a2718aa46708c803ce49318619982ba06a6615d24bb853c0fb85ebed833a802245e4518d4e2ba10da1"
      "f22c732505433c558bed8895eb1e97cb5d65f821be9330143e93a738ef6896165879f692d75c2d7928e01"
      "fd7fe601d16931bdd876c7b15b741e48546fe80db45df56e22ed2fa974ab937af7644d20834f41a61aeb9"
      "a70d0248d274642b14ed6585892403bed8e03a9a12485ae44e3d39ab53e5bd70dee58476fb81860a18679"
      "9429b71f79f204894cf21d31cc19118d547bb1b946532d080e074ec97e23667818490203010001";

  // PEM format of the same public key (converted from DER above)
  std::string pem_key = "-----BEGIN PUBLIC KEY-----\n"
                        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuhDr4YVGVYYJMij7OwCT\n"
                        "xWCFO36/KEl677mWGmzIht0/bTJ4qTJE+lCEqcJjvVf+tOoYaKqKJxiqRnCMgDzk\n"
                        "kxhhmYK6BqZhXSS7hTwPuF6+2DOoAiReRRjU4roQ2h8ixzJQVDPFWL7YiV6x6Xy1\n"
                        "1l+CG+kzAUPpOnOO9olhZYefaS11wteSjgH9f+YB0Wkxvdh2x7FbdB5IVG/oDbRd\n"
                        "9W4i7S+pdKuTevdkTSCDT0GmGuuacNAkjSdGQrFO1lhYkkA77Y4DqaEkha5E49Oa\n"
                        "tT5b1w3uWEdvuBhgoYZ5lCm3H3nyBIlM8h0xzBkRjVR7sblGUy0IDgdOyX4jZngY\n"
                        "SQIDAQAB\n"
                        "-----END PUBLIC KEY-----";

  auto data = "hello\n";
  std::vector<uint8_t> text(data, data + strlen(data));

  // Import both DER and PEM public keys
  Common::Crypto::PKeyObjectPtr der_crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPublicKeyDER(Hex::decode(der_key)));
  Common::Crypto::PKeyObject* der_crypto = der_crypto_ptr.get();

  Common::Crypto::PKeyObjectPtr pem_crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPublicKeyPEM(pem_key));
  Common::Crypto::PKeyObject* pem_crypto = pem_crypto_ptr.get();

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

  // Test verification with both DER and PEM public key formats
  std::vector<std::pair<std::string, Common::Crypto::PKeyObject*>> key_formats = {
      {"DER public key", der_crypto}, {"PEM public key", pem_crypto}};

  for (const auto& format : key_formats) {
    const std::string& description = format.first;
    Common::Crypto::PKeyObject* crypto = format.second;

    // Loop through each hash function and its signature
    for (const auto& entry : hashSignatures) {
      const std::string& hash_func = entry.first;
      const std::string& signature = entry.second;
      auto sig = Hex::decode(signature);

      auto result = UtilitySingleton::get().verifySignature(hash_func, *crypto, sig, text);
      ASSERT_TRUE(result.ok()) << "Verification failed for " << description << " with " << hash_func
                               << ": " << result.message();
    }
  }

  auto signature =
      "504c24addc615c01c915e3244b8248b470a41c82faa6a82526ddd8f21e197adae6c8b985e40960157d47f336d8ba"
      "31ce4b3b1795379a7415af57daa757d3027e870b3c6644e749b583c51a16f9984afd39c909325d271d8d4c8d0062"
      "88bd8f7945aa24a783613ecece95a9692b3b56dd1d831fc06d82eca40fd432a15a6cdb837d7ce378ac889c4ab00b"
      "0c1f9c2be279952696b70c9ea2bb014d6f20b72ed4917904d5f24d5776058bd11121f3ed02e03c384cf427346b1d"
      "300867969f22e27aa9f0607344cc9d8e9a90802e97ac39af9324f60ddad24682e48424346dd5a53fe550370bdf9f"
      "94dec8a80810edd648a33cc767f9e4328660e3ee1be8b47e9cfa";
  auto sig = Hex::decode(signature);

  // Test error cases using DER public key
  auto result = UtilitySingleton::get().verifySignature("unknown", *der_crypto, sig, text);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ("unknown is not supported.", result.message());

  // Test with an empty crypto object
  auto empty_crypto = std::make_unique<PKeyObject>();
  result = UtilitySingleton::get().verifySignature("sha256", *empty_crypto, sig, text);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ("Failed to initialize digest verify.", result.message());

  // Test with incorrect data
  auto bad_data = "baddata";
  std::vector<uint8_t> bad_text(bad_data, bad_data + strlen(bad_data));
  result = UtilitySingleton::get().verifySignature("sha256", *der_crypto, sig, bad_text);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ("Failed to verify digest. Error code: 0", result.message());

  // Test with incorrect signature
  auto good_data = "hello";
  std::vector<uint8_t> good_text(good_data, good_data + strlen(good_data));
  result = UtilitySingleton::get().verifySignature("sha256", *der_crypto, Hex::decode("000000"),
                                                   good_text);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ("Failed to verify digest. Error code: 0", result.message());
}

TEST(UtilityTest, TestImportPrivateKey) {
  auto key =
      "308204be020100300d06092a864886f70d0101010500048204a8308204a40201000282010100ce7901c29654f7e0"
      "4e0802cf6410c9e354ce0bcaafa6de2521e453f0f3f8c07607389bbc6aaba22e41bff51244d0a7b87d1d271d27da"
      "98d16b324d0ace80bc9c236c33c24a96e7009b4e2e618d2449130415e4001cc08e5daca7b5794ed61fee1db5bf87"
      "9a29ece0ec2af927819e5a5c37e45c0fc3ae13adf3992828e4d97d7d7b5bfd7a0631812f2badd1ba6c6f88cfd767"
      "e53d64f47ac4f61525e435db626356570f1e02ff0ce4d7bb92bd865edfd0f3978a7ccc059c034a6065cf917821da"
      "e0b9a721df188b744151ce8cc289625b8186f68aba5290b8d5686d8b7f66231328db9a42d5c03c24685a0922aa9e"
      "34d95e643e11555598d620cc1f7185a5d4170203010001028201004d5cf1d7e3543afc84c063ad29a550c0294a7b"
      "089b003f44528aa7192591132c265083a9f99e0dca9f4039a77ab963deb0a277c168e9735124855870b02774845c"
      "9172635e67646ec9c265868fc804c967427c87be3e3819c9539d9fb27670c85bc179de6959443492c9174a423aff"
      "488678be35f9f003d7adeab92d7972349e5f5a4d21ecc9eecb812132dfcec4477454e09c07f51684df4720e04a9e"
      "24362db8cd2196c1804782a682174b4dc977a84eb27c1f664f22eb64b3abae433d045fb4eea3730bc4ef30d0fb85"
      "98471dea2c78f654ebcded8b7436155c1f03362e8409c0636022b8116bced4c46099c53fa4d8d8d1f4f6be7775fd"
      "448ea888444da102818100f11fb88f8514202d4e3b137270f3cb98d8e17fc9caf77c76eda9a1bc0e2cebc4c3997a"
      "bf96bcdb945beede3e01d6464913f446d594218677619ecdb584b63dca81cacd9fa9030a00d5bb143483b8aaa86a"
      "7d8616adc16645376c8904e259e784e5fced37135ea8f776940cd3371550acdc1af2d409bfc1ad7253ab1541540f"
      "dd02818100db3602515c160b41803d732afbbb8f411fc024648932e44e7dd8e728cbfe7bc5282a6f57027964c8ba"
      "22618a83f1161d187251efd5de3bb7c83d50db6295b1392e9e87c205761858daed057317d815cafe52253eaf2f72"
      "6897965ed46f0a212d8355a2d2e64882e9e32166cca7e4336cc3b279ace0f67abee126e39087682e8302818100ec"
      "091b481303a283f722c964abc15bba62044c6da32c2540de61c19b2f5d35e6c57ac6b829bcf24e06b88c01b316a8"
      "72fcff911f9e043b773dae90bc720f5be992a88e250ef394a5409403b16c882736fa17aa5d24f63f40de827696bb"
      "653ac7d3c3860af60121f22cb7bcde3dfbb59fa14f180a0d091374d087aae001b5625902818011561922d4148e39"
      "54ea0734ac09ee4f693269ee658757d4f950f11f21daf370e93749ece8ae2f114cdf3135a22fabdf0b32e755ff64"
      "fef60ee9027f0731ed7d2739b464dcc7b52f39c92af82a3795a9a3295df6b2261f77341dd94c15a8086db00852c3"
      "39211cf1605c20e42896fc962a77eff583291b16037a6ededc4699ff02818100cadc0cbd4e4f00301e3594190529"
      "c8324c19ed77138b7582288a229f86c6f261f95b93d47a318856b3585e68b1b90be6c8467a4e8f97f6e820064f8d"
      "2793ddf93e1cfa119f1f166de15d6588d9e8ac5ffd30c953374c22557d3f80d24982425dfe00754cfab810c8ff12"
      "6adfb09964d360d1d2d337cf3076c53e4d59f911feee";

  Common::Crypto::PKeyObjectPtr crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPrivateKeyDER(Hex::decode(key)));
  EVP_PKEY* pkey = crypto_ptr->getEVP_PKEY();
  EXPECT_NE(nullptr, pkey);

  std::vector<uint8_t> bad_key = {0x1, 0x2, 0x3, 0x4, 0x5};
  crypto_ptr = Common::Crypto::UtilitySingleton::get().importPrivateKeyDER(bad_key);
  pkey = crypto_ptr->getEVP_PKEY();
  EXPECT_EQ(nullptr, pkey);

  EVP_PKEY* empty_pkey = EVP_PKEY_new();
  crypto_ptr->setEVP_PKEY(empty_pkey);
  pkey = crypto_ptr->getEVP_PKEY();
  EXPECT_NE(nullptr, pkey);
}

TEST(UtilityTest, TestSign) {
  auto private_key =
      "308204be020100300d06092a864886f70d0101010500048204a8308204a40201000282010100ce7901c29654f7e0"
      "4e0802cf6410c9e354ce0bcaafa6de2521e453f0f3f8c07607389bbc6aaba22e41bff51244d0a7b87d1d271d27da"
      "98d16b324d0ace80bc9c236c33c24a96e7009b4e2e618d2449130415e4001cc08e5daca7b5794ed61fee1db5bf87"
      "9a29ece0ec2af927819e5a5c37e45c0fc3ae13adf3992828e4d97d7d7b5bfd7a0631812f2badd1ba6c6f88cfd767"
      "e53d64f47ac4f61525e435db626356570f1e02ff0ce4d7bb92bd865edfd0f3978a7ccc059c034a6065cf917821da"
      "e0b9a721df188b744151ce8cc289625b8186f68aba5290b8d5686d8b7f66231328db9a42d5c03c24685a0922aa9e"
      "34d95e643e11555598d620cc1f7185a5d4170203010001028201004d5cf1d7e3543afc84c063ad29a550c0294a7b"
      "089b003f44528aa7192591132c265083a9f99e0dca9f4039a77ab963deb0a277c168e9735124855870b02774845c"
      "9172635e67646ec9c265868fc804c967427c87be3e3819c9539d9fb27670c85bc179de6959443492c9174a423aff"
      "488678be35f9f003d7adeab92d7972349e5f5a4d21ecc9eecb812132dfcec4477454e09c07f51684df4720e04a9e"
      "24362db8cd2196c1804782a682174b4dc977a84eb27c1f664f22eb64b3abae433d045fb4eea3730bc4ef30d0fb85"
      "98471dea2c78f654ebcded8b7436155c1f03362e8409c0636022b8116bced4c46099c53fa4d8d8d1f4f6be7775fd"
      "448ea888444da102818100f11fb88f8514202d4e3b137270f3cb98d8e17fc9caf77c76eda9a1bc0e2cebc4c3997a"
      "bf96bcdb945beede3e01d6464913f446d594218677619ecdb584b63dca81cacd9fa9030a00d5bb143483b8aaa86a"
      "7d8616adc16645376c8904e259e784e5fced37135ea8f776940cd3371550acdc1af2d409bfc1ad7253ab1541540f"
      "dd02818100db3602515c160b41803d732afbbb8f411fc024648932e44e7dd8e728cbfe7bc5282a6f57027964c8ba"
      "22618a83f1161d187251efd5de3bb7c83d50db6295b1392e9e87c205761858daed057317d815cafe52253eaf2f72"
      "6897965ed46f0a212d8355a2d2e64882e9e32166cca7e4336cc3b279ace0f67abee126e39087682e8302818100ec"
      "091b481303a283f722c964abc15bba62044c6da32c2540de61c19b2f5d35e6c57ac6b829bcf24e06b88c01b316a8"
      "72fcff911f9e043b773dae90bc720f5be992a88e250ef394a5409403b16c882736fa17aa5d24f63f40de827696bb"
      "653ac7d3c3860af60121f22cb7bcde3dfbb59fa14f180a0d091374d087aae001b5625902818011561922d4148e39"
      "54ea0734ac09ee4f693269ee658757d4f950f11f21daf370e93749ece8ae2f114cdf3135a22fabdf0b32e755ff64"
      "fef60ee9027f0731ed7d2739b464dcc7b52f39c92af82a3795a9a3295df6b2261f77341dd94c15a8086db00852c3"
      "39211cf1605c20e42896fc962a77eff583291b16037a6ededc4699ff02818100cadc0cbd4e4f00301e3594190529"
      "c8324c19ed77138b7582288a229f86c6f261f95b93d47a318856b3585e68b1b90be6c8467a4e8f97f6e820064f8d"
      "2793ddf93e1cfa119f1f166de15d6588d9e8ac5ffd30c953374c22557d3f80d24982425dfe00754cfab810c8ff12"
      "6adfb09964d360d1d2d337cf3076c53e4d59f911feee";
  auto data = "hello\n";

  Common::Crypto::PKeyObjectPtr crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPrivateKeyDER(Hex::decode(private_key)));
  Common::Crypto::PKeyObject* crypto(crypto_ptr.get());

  std::vector<uint8_t> text(data, data + strlen(data));

  // Import PEM private key for format equivalence testing
  std::string pem_private_key = "-----BEGIN PRIVATE KEY-----\n"
                                "MIIEvgIBADANBgkqhkiG9w0BAQEFAASCBKgwggSkAgEAAoIBAQDOeQHCllT34E4I\n"
                                "As9kEMnjVM4Lyq+m3iUh5FPw8/jAdgc4m7xqq6IuQb/1EkTQp7h9HScdJ9qY0Wsy\n"
                                "TQrOgLycI2wzwkqW5wCbTi5hjSRJEwQV5AAcwI5drKe1eU7WH+4dtb+Hmins4Owq\n"
                                "+SeBnlpcN+RcD8OuE63zmSgo5Nl9fXtb/XoGMYEvK63RumxviM/XZ+U9ZPR6xPYV\n"
                                "JeQ122JjVlcPHgL/DOTXu5K9hl7f0POXinzMBZwDSmBlz5F4Idrguach3xiLdEFR\n"
                                "zozCiWJbgYb2irpSkLjVaG2Lf2YjEyjbmkLVwDwkaFoJIqqeNNleZD4RVVWY1iDM\n"
                                "H3GFpdQXAgMBAAECggEATVzx1+NUOvyEwGOtKaVQwClKewibAD9EUoqnGSWREywm\n"
                                "UIOp+Z4Nyp9AOad6uWPesKJ3wWjpc1EkhVhwsCd0hFyRcmNeZ2RuycJlho/IBMln\n"
                                "QnyHvj44GclTnZ+ydnDIW8F53mlZRDSSyRdKQjr/SIZ4vjX58APXreq5LXlyNJ5f\n"
                                "Wk0h7Mnuy4EhMt/OxEd0VOCcB/UWhN9HIOBKniQ2LbjNIZbBgEeCpoIXS03Jd6hO\n"
                                "snwfZk8i62Szq65DPQRftO6jcwvE7zDQ+4WYRx3qLHj2VOvN7Yt0NhVcHwM2LoQJ\n"
                                "wGNgIrgRa87UxGCZxT+k2NjR9Pa+d3X9RI6oiERNoQKBgQDxH7iPhRQgLU47E3Jw\n"
                                "88uY2OF/ycr3fHbtqaG8DizrxMOZer+WvNuUW+7ePgHWRkkT9EbVlCGGd2GezbWE\n"
                                "tj3KgcrNn6kDCgDVuxQ0g7iqqGp9hhatwWZFN2yJBOJZ54Tl/O03E16o93aUDNM3\n"
                                "FVCs3Bry1Am/wa1yU6sVQVQP3QKBgQDbNgJRXBYLQYA9cyr7u49BH8AkZIky5E59\n"
                                "2Ocoy/57xSgqb1cCeWTIuiJhioPxFh0YclHv1d47t8g9UNtilbE5Lp6HwgV2GFja\n"
                                "7QVzF9gVyv5SJT6vL3Jol5Ze1G8KIS2DVaLS5kiC6eMhZsyn5DNsw7J5rOD2er7h\n"
                                "JuOQh2gugwKBgQDsCRtIEwOig/ciyWSrwVu6YgRMbaMsJUDeYcGbL1015sV6xrgp\n"
                                "vPJOBriMAbMWqHL8/5EfngQ7dz2ukLxyD1vpkqiOJQ7zlKVAlAOxbIgnNvoXql0k\n"
                                "9j9A3oJ2lrtlOsfTw4YK9gEh8iy3vN49+7WfoU8YCg0JE3TQh6rgAbViWQKBgBFW\n"
                                "GSLUFI45VOoHNKwJ7k9pMmnuZYdX1PlQ8R8h2vNw6TdJ7OiuLxFM3zE1oi+r3wsy\n"
                                "51X/ZP72DukCfwcx7X0nObRk3Me1LznJKvgqN5Wpoyld9rImH3c0HdlMFagIbbAI\n"
                                "UsM5IRzxYFwg5CiW/JYqd+/1gykbFgN6bt7cRpn/AoGBAMrcDL1OTwAwHjWUGQUp\n"
                                "yDJMGe13E4t1giiKIp+GxvJh+VuT1HoxiFazWF5osbkL5shGek6Pl/boIAZPjSeT\n"
                                "3fk+HPoRnx8WbeFdZYjZ6Kxf/TDJUzdMIlV9P4DSSYJCXf4AdUz6uBDI/xJq37CZ\n"
                                "ZNNg0dLTN88wdsU+TVn5Ef7u\n"
                                "-----END PRIVATE KEY-----";

  Common::Crypto::PKeyObjectPtr pem_private_crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPrivateKeyPEM(pem_private_key));
  Common::Crypto::PKeyObject* pem_private_crypto = pem_private_crypto_ptr.get();

  // Test signing with different hash functions
  std::vector<std::string> hash_functions = {"sha1", "sha224", "sha256", "sha384", "sha512"};

  for (const auto& hash_func : hash_functions) {
    auto result = UtilitySingleton::get().sign(hash_func, *crypto, text);
    ASSERT_TRUE(result.ok()) << "Signing failed with " << hash_func << ": " << result.status();
    EXPECT_FALSE(result->empty());

    // Test format equivalence: PEM private key should produce identical signature
    auto pem_result = UtilitySingleton::get().sign(hash_func, *pem_private_crypto, text);
    ASSERT_TRUE(pem_result.ok()) << "PEM signing failed with " << hash_func << ": "
                                 << pem_result.status();
    EXPECT_FALSE(pem_result->empty()) << "PEM signature empty with " << hash_func;

    // Verify signatures are identical (validates format equivalence)
    EXPECT_EQ(*result, *pem_result)
        << "DER and PEM signatures differ for " << hash_func << " - format equivalence broken!";

    // Verify the signature can be verified with the corresponding public key
    auto public_key =
        "30820122300d06092a864886f70d01010105000382010f003082010a0282010100ce7901c29654f7e04e0802cf"
        "6410c9e354ce0bcaafa6de2521e453f0f3f8c07607389bbc6aaba22e41bff51244d0a7b87d1d271d27da98d16b"
        "324d0ace80bc9c236c33c24a96e7009b4e2e618d2449130415e4001cc08e5daca7b5794ed61fee1db5bf879a29"
        "ece0ec2af927819e5a5c37e45c0fc3ae13adf3992828e4d97d7d7b5bfd7a0631812f2badd1ba6c6f88cfd767e5"
        "3d64f47ac4f61525e435db626356570f1e02ff0ce4d7bb92bd865edfd0f3978a7ccc059c034a6065cf917821da"
        "e0b9a721df188b744151ce8cc289625b8186f68aba5290b8d5686d8b7f66231328db9a42d5c03c24685a0922aa"
        "9e34d95e643e11555598d620cc1f7185a5d4170203010001";

    Common::Crypto::PKeyObjectPtr public_crypto_ptr(
        Common::Crypto::UtilitySingleton::get().importPublicKeyDER(Hex::decode(public_key)));
    Common::Crypto::PKeyObject* public_crypto(public_crypto_ptr.get());

    auto verify_result =
        UtilitySingleton::get().verifySignature(hash_func, *public_crypto, *result, text);
    ASSERT_TRUE(verify_result.ok());

    // Also verify with PEM format of the same public key (demonstrates format interoperability)
    std::string pem_public_key =
        "-----BEGIN PUBLIC KEY-----\n"
        "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAznkBwpZU9+BOCALPZBDJ\n"
        "41TOC8qvpt4lIeRT8PP4wHYHOJu8aquiLkG/9RJE0Ke4fR0nHSfamNFrMk0KzoC8\n"
        "nCNsM8JKlucAm04uYY0kSRMEFeQAHMCOXayntXlO1h/uHbW/h5op7ODsKvkngZ5a\n"
        "XDfkXA/DrhOt85koKOTZfX17W/16BjGBLyut0bpsb4jP12flPWT0esT2FSXkNdti\n"
        "Y1ZXDx4C/wzk17uSvYZe39Dzl4p8zAWcA0pgZc+ReCHa4LmnId8Yi3RBUc6Mwoli\n"
        "W4GG9oq6UpC41Whti39mIxMo25pC1cA8JGhaCSKqnjTZXmQ+EVVVmNYgzB9xhaXU\n"
        "FwIDAQAB\n"
        "-----END PUBLIC KEY-----";

    Common::Crypto::PKeyObjectPtr pem_public_crypto_ptr(
        Common::Crypto::UtilitySingleton::get().importPublicKeyPEM(pem_public_key));
    Common::Crypto::PKeyObject* pem_public_crypto(pem_public_crypto_ptr.get());

    auto pem_verify_result =
        UtilitySingleton::get().verifySignature(hash_func, *pem_public_crypto, *result, text);
    ASSERT_TRUE(pem_verify_result.ok())
        << "PEM verification failed with " << hash_func << ": " << pem_verify_result.message();
  }

  // Test with unknown hash function
  auto result = UtilitySingleton::get().sign("unknown", *crypto, text);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ("unknown is not supported.", result.status().message());

  // Test with empty crypto object
  auto empty_crypto = std::make_unique<PKeyObject>();
  result = UtilitySingleton::get().sign("sha256", *empty_crypto, text);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ("Invalid key type: private key required for signing operation.",
            result.status().message());

  // Test with empty text
  std::vector<uint8_t> empty_text;
  result = UtilitySingleton::get().sign("sha256", *crypto, empty_text);
  ASSERT_TRUE(result.ok());
  EXPECT_FALSE(result->empty());
}

TEST(UtilityTest, TestHashFunctionSupport) {
  // Test hash function support through sign/verify operations
  // This indirectly tests getHashFunction() coverage

  auto private_key =
      "308204be020100300d06092a864886f70d0101010500048204a8308204a40201000282010100ce7901c29654f7e0"
      "4e0802cf6410c9e354ce0bcaafa6de2521e453f0f3f8c07607389bbc6aaba22e41bff51244d0a7b87d1d271d27da"
      "98d16b324d0ace80bc9c236c33c24a96e7009b4e2e618d2449130415e4001cc08e5daca7b5794ed61fee1db5bf87"
      "9a29ece0ec2af927819e5a5c37e45c0fc3ae13adf3992828e4d97d7d7b5bfd7a0631812f2badd1ba6c6f88cfd767"
      "e53d64f47ac4f61525e435db626356570f1e02ff0ce4d7bb92bd865edfd0f3978a7ccc059c034a6065cf917821da"
      "e0b9a721df188b744151ce8cc289625b8186f68aba5290b8d5686d8b7f66231328db9a42d5c03c24685a0922aa9e"
      "34d95e643e11555598d620cc1f7185a5d4170203010001028201004d5cf1d7e3543afc84c063ad29a550c0294a7b"
      "089b003f44528aa7192591132c265083a9f99e0dca9f4039a77ab963deb0a277c168e9735124855870b02774845c"
      "9172635e67646ec9c265868fc804c967427c87be3e3819c9539d9fb27670c85bc179de6959443492c9174a423aff"
      "488678be35f9f003d7adeab92d7972349e5f5a4d21ecc9eecb812132dfcec4477454e09c07f51684df4720e04a9e"
      "24362db8cd2196c1804782a682174b4dc977a84eb27c1f664f22eb64b3abae433d045fb4eea3730bc4ef30d0fb85"
      "98471dea2c78f654ebcded8b7436155c1f03362e8409c0636022b8116bced4c46099c53fa4d8d8d1f4f6be7775fd"
      "448ea888444da102818100f11fb88f8514202d4e3b137270f3cb98d8e17fc9caf77c76eda9a1bc0e2cebc4c3997a"
      "bf96bcdb945beede3e01d6464913f446d594218677619ecdb584b63dca81cacd9fa9030a00d5bb143483b8aaa86a"
      "7d8616adc16645376c8904e259e784e5fced37135ea8f776940cd3371550acdc1af2d409bfc1ad7253ab1541540f"
      "dd02818100db3602515c160b41803d732afbbb8f411fc024648932e44e7dd8e728cbfe7bc5282a6f57027964c8ba"
      "22618a83f1161d187251efd5de3bb7c83d50db6295b1392e9e87c205761858daed057317d815cafe52253eaf2f72"
      "6897965ed46f0a212d8355a2d2e64882e9e32166cca7e4336cc3b279ace0f67abee126e39087682e8302818100ec"
      "091b481303a283f722c964abc15bba62044c6da32c2540de61c19b2f5d35e6c57ac6b829bcf24e06b88c01b316a8"
      "72fcff911f9e043b773dae90bc720f5be992a88e250ef394a5409403b16c882736fa17aa5d24f63f40de827696bb"
      "653ac7d3c3860af60121f22cb7bcde3dfbb59fa14f180a0d091374d087aae001b5625902818011561922d4148e39"
      "54ea0734ac09ee4f693269ee658757d4f950f11f21daf370e93749ece8ae2f114cdf3135a22fabdf0b32e755ff64"
      "fef60ee9027f0731ed7d2739b464dcc7b52f39c92af82a3795a9a3295df6b2261f77341dd94c15a8086db00852c3"
      "39211cf1605c20e42896fc962a77eff583291b16037a6ededc4699ff02818100cadc0cbd4e4f00301e3594190529"
      "c8324c19ed77138b7582288a229f86c6f261f95b93d47a318856b3585e68b1b90be6c8467a4e8f97f6e820064f8d"
      "2793ddf93e1cfa119f1f166de15d6588d9e8ac5ffd30c953374c22557d3f80d24982425dfe00754cfab810c8ff12"
      "6adfb09964d360d1d2d337cf3076c53e4d59f911feee";

  Common::Crypto::PKeyObjectPtr crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPrivateKeyDER(Hex::decode(private_key)));
  Common::Crypto::PKeyObject* crypto(crypto_ptr.get());

  auto data = "test hash functions";
  std::vector<uint8_t> text(data, data + strlen(data));

  // Test all supported hash functions (this exercises getHashFunction internally)
  std::vector<std::string> supported_hashes = {"sha1", "sha224", "sha256", "sha384", "sha512"};
  for (const auto& hash : supported_hashes) {
    auto result = UtilitySingleton::get().sign(hash, *crypto, text);
    ASSERT_TRUE(result.ok()) << "Signing failed with " << hash << ": " << result.status();
    EXPECT_FALSE(result->empty()) << "Signature empty with " << hash;
  }

  // Test case insensitive hash functions
  std::vector<std::string> case_variants = {"SHA1", "SHA256", "Sha384"};
  for (const auto& hash : case_variants) {
    auto result = UtilitySingleton::get().sign(hash, *crypto, text);
    ASSERT_TRUE(result.ok()) << "Case insensitive signing failed with " << hash << ": "
                             << result.status();
    EXPECT_FALSE(result->empty()) << "Case insensitive signature empty with " << hash;
  }

  // Test unsupported hash functions
  std::vector<std::string> unsupported_hashes = {"md5", "sha3", "unknown", ""};
  for (const auto& hash : unsupported_hashes) {
    auto result = UtilitySingleton::get().sign(hash, *crypto, text);
    EXPECT_FALSE(result.ok()) << "Unsupported hash should fail: " << hash;
    EXPECT_EQ(hash + " is not supported.", result.status().message())
        << "Wrong error message for " << hash;
  }

  // Test additional edge cases for hash function support
  // Test with very long hash function names
  std::string long_hash_name(1000, 'a');
  auto result = UtilitySingleton::get().sign(long_hash_name, *crypto, text);
  EXPECT_FALSE(result.ok()) << "Very long hash name should not be supported";
  EXPECT_EQ(long_hash_name + " is not supported.", result.status().message());

  // Test with hash names containing special characters
  std::vector<std::string> special_hashes = {"sha-256", "sha_256", "sha.256", "sha256!",
                                             "sha256@#$"};
  for (const auto& hash : special_hashes) {
    auto result = UtilitySingleton::get().sign(hash, *crypto, text);
    EXPECT_FALSE(result.ok()) << "Hash with special characters should not be supported: " << hash;
    EXPECT_EQ(hash + " is not supported.", result.status().message());
  }
}

TEST(UtilityTest, TestBoundaryConditions) {
  // Test with maximum size buffers to ensure all code paths in getSha256Digest
  Buffer::OwnedImpl large_buffer;
  for (int i = 0; i < 1000; ++i) {
    large_buffer.add("This is a large buffer to test digest computation with multiple slices. ");
  }

  auto digest = UtilitySingleton::get().getSha256Digest(large_buffer);
  EXPECT_EQ(32, digest.size()) << "SHA256 digest should always be 32 bytes";
  EXPECT_FALSE(digest.empty()) << "Digest should not be empty";

  // Test HMAC with large key and message
  std::vector<uint8_t> large_key(1024, 0xAB);
  std::string large_message(10000, 'X');
  auto hmac = UtilitySingleton::get().getSha256Hmac(large_key, large_message);
  EXPECT_EQ(32, hmac.size()) << "HMAC should always be 32 bytes";
  EXPECT_FALSE(hmac.empty()) << "HMAC should not be empty";

  // Test with zero-length key and message
  std::vector<uint8_t> empty_key;
  std::string empty_message;
  auto empty_hmac = UtilitySingleton::get().getSha256Hmac(empty_key, empty_message);
  EXPECT_EQ(32, empty_hmac.size()) << "HMAC with empty inputs should still be 32 bytes";
}

TEST(UtilityTest, TestPEMParsingFailures) {
  // Test PEM parsing with malformed PEM data to exercise error paths

  // Invalid PEM public key (has markers but corrupted content)
  std::string invalid_pem_public = "-----BEGIN PUBLIC KEY-----\n"
                                   "INVALID_BASE64_CONTENT_HERE\n"
                                   "-----END PUBLIC KEY-----";

  auto crypto_ptr = UtilitySingleton::get().importPublicKeyPEM(invalid_pem_public);
  EVP_PKEY* pkey = crypto_ptr->getEVP_PKEY();
  EXPECT_EQ(nullptr, pkey) << "Invalid PEM public key should fail to parse";

  // Invalid PEM private key (has markers but corrupted content)
  std::string invalid_pem_private = "-----BEGIN PRIVATE KEY-----\n"
                                    "INVALID_BASE64_CONTENT_HERE\n"
                                    "-----END PRIVATE KEY-----";

  auto private_crypto_ptr = UtilitySingleton::get().importPrivateKeyPEM(invalid_pem_private);
  EVP_PKEY* private_pkey = private_crypto_ptr->getEVP_PKEY();
  EXPECT_EQ(nullptr, private_pkey) << "Invalid PEM private key should fail to parse";

  // Test with very large key data to exercise string construction path
  std::vector<uint8_t> large_key_data(50000, 'A'); // 50KB of 'A' characters
  large_key_data[0] = '-';
  large_key_data[1] = '-'; // Make it look like PEM start
  auto large_crypto_ptr = UtilitySingleton::get().importPublicKeyDER(large_key_data);
  EVP_PKEY* large_pkey = large_crypto_ptr->getEVP_PKEY();
  EXPECT_EQ(nullptr, large_pkey) << "Large invalid key should fail to parse";
}

TEST(UtilityTest, TestHelperFunctions) {
  // Test helper functions directly to improve coverage
  auto impl = std::make_unique<UtilityImpl>();

  // Test empty key string (exercises BIO allocation with edge case)
  std::string empty_key;
  auto empty_public_pem = impl->importPublicKeyPEM(empty_key);
  EXPECT_EQ(nullptr, empty_public_pem->getEVP_PKEY()) << "Empty PEM key should fail";

  auto empty_private_pem = impl->importPrivateKeyPEM(empty_key);
  EXPECT_EQ(nullptr, empty_private_pem->getEVP_PKEY()) << "Empty PEM private key should fail";

  // Test DER functions directly with various invalid inputs
  std::vector<uint8_t> invalid_der = {0x30, 0x82}; // Truncated DER
  auto der_public = impl->importPublicKeyDER(invalid_der);
  EXPECT_EQ(nullptr, der_public->getEVP_PKEY()) << "Invalid DER key should fail";

  auto der_private = impl->importPrivateKeyDER(invalid_der);
  EXPECT_EQ(nullptr, der_private->getEVP_PKEY()) << "Invalid DER private key should fail";
}

TEST(UtilityTest, ImportPublicKeyPEM_WithNullData_Fails) {
  auto impl = std::make_unique<UtilityImpl>();

  // Test with null data pointer (edge case)
  std::string null_data(100, '\0'); // 100 null characters
  auto result = impl->importPublicKeyPEM(null_data);
  EXPECT_EQ(nullptr, result->getEVP_PKEY()) << "Null data should fail PEM import";
}

TEST(UtilityTest, ImportPrivateKeyPEM_WithNullData_Fails) {
  auto impl = std::make_unique<UtilityImpl>();

  // Test with null data pointer (edge case)
  std::string null_data(100, '\0'); // 100 null characters
  auto result = impl->importPrivateKeyPEM(null_data);
  EXPECT_EQ(nullptr, result->getEVP_PKEY()) << "Null data should fail private PEM import";
}

TEST(UtilityTest, ImportPublicKeyPEM_WithMalformedBase64_Fails) {
  auto impl = std::make_unique<UtilityImpl>();

  // Test with data that looks like PEM but has invalid structure
  std::string malformed_pem = "-----BEGIN PUBLIC KEY-----\n"
                              "This is not valid base64 content\n"
                              "-----END PUBLIC KEY-----";

  auto result = impl->importPublicKeyPEM(malformed_pem);
  EXPECT_EQ(nullptr, result->getEVP_PKEY()) << "Malformed PEM should fail";
}

TEST(UtilityTest, ImportPrivateKeyPEM_WithMalformedBase64_Fails) {
  auto impl = std::make_unique<UtilityImpl>();

  // Test with data that looks like PEM but has invalid structure
  std::string malformed_pem = "-----BEGIN PUBLIC KEY-----\n"
                              "This is not valid base64 content\n"
                              "-----END PUBLIC KEY-----";

  auto result = impl->importPrivateKeyPEM(malformed_pem);
  EXPECT_EQ(nullptr, result->getEVP_PKEY()) << "Malformed private PEM should fail";
}

TEST(UtilityTest, ImportPublicKeyPEMWithEmptyInput) {
  auto impl = std::make_unique<UtilityImpl>();

  std::string empty;
  auto empty_pem = impl->importPublicKeyPEM(empty);
  EXPECT_EQ(nullptr, empty_pem->getEVP_PKEY()) << "Empty PEM should fail";
}

TEST(UtilityTest, ImportPublicKeyDERWithEmptyInput) {
  auto impl = std::make_unique<UtilityImpl>();

  std::vector<uint8_t> empty_vec;
  auto empty_der = impl->importPublicKeyDER(empty_vec);
  EXPECT_EQ(nullptr, empty_der->getEVP_PKEY()) << "Empty DER should fail";
}

TEST(UtilityTest, ImportPublicKeyPEMWithInvalidFormat) {
  auto impl = std::make_unique<UtilityImpl>();

  // Test with single character
  std::string single = "A";
  auto single_pem = impl->importPublicKeyPEM(single);
  EXPECT_EQ(nullptr, single_pem->getEVP_PKEY()) << "Single char PEM should fail";

  // Test with invalid PEM format (missing newlines)
  std::string invalid_pem = "-----BEGIN PUBLIC KEY-----CONTENT-----END PUBLIC KEY-----";
  auto invalid_pem_result = impl->importPublicKeyPEM(invalid_pem);
  EXPECT_EQ(nullptr, invalid_pem_result->getEVP_PKEY()) << "Invalid PEM should fail";
}

TEST(UtilityTest, ImportPublicKeyDERWithInvalidFormat) {
  auto impl = std::make_unique<UtilityImpl>();

  // Test with single character
  std::vector<uint8_t> single_vec = {'A'};
  auto single_der = impl->importPublicKeyDER(single_vec);
  EXPECT_EQ(nullptr, single_der->getEVP_PKEY()) << "Single char DER should fail";
}

TEST(UtilityTest, ImportPublicKeyPEMWithValidFormat) {
  auto impl = std::make_unique<UtilityImpl>();

  // Test with valid PEM format
  std::string valid_pem = "-----BEGIN PUBLIC KEY-----\n"
                          "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAp0cSZtAdFgMI1zQJwG8u\n"
                          "jTXFMcRY0+SA6fMZGEfQYuxcz/e8UelJ1fLDVAwYmk7KHoYzpizy0JIxAcJ+OAE+\n"
                          "cd6a6RpwSEm/9/vizlv0vWZv2XMRAqUxk/5amlpQZE/4sRg/qJdkZZjKrSKjf5VE\n"
                          "UQg2NytExYyYWG+3FEYpzYyUeVktmW0y/205XAuEQuxaoe+AUVKeoON1iDzvxywE\n"
                          "42C0749XYGUFicqBSRj2eO7jm4hNWvgTapYwpswM3hV9yOAPOVQGKNXzNbLDbFTH\n"
                          "yLw3OKayGs/4FUBa+ijlGD9VDawZq88RRaf5ztmH22gOSiKcrHXe40fsnrzh/D27\n"
                          "uwIDAQAB\n"
                          "-----END PUBLIC KEY-----";
  auto valid_pem_result = impl->importPublicKeyPEM(valid_pem);
  EXPECT_NE(nullptr, valid_pem_result->getEVP_PKEY()) << "Valid PEM should succeed";
}

TEST(UtilityTest, ImportKeysDERWithInvalidData) {
  auto impl = std::make_unique<UtilityImpl>();

  // Test DER import functions with various invalid inputs
  std::vector<uint8_t> invalid_der = {0x30, 0x01, 0x02}; // Invalid DER
  auto der_public = impl->importPublicKeyDER(invalid_der);
  EXPECT_EQ(nullptr, der_public->getEVP_PKEY()) << "Invalid DER public key should fail";

  auto der_private = impl->importPrivateKeyDER(invalid_der);
  EXPECT_EQ(nullptr, der_private->getEVP_PKEY()) << "Invalid DER private key should fail";

  // Test with single byte DER data
  std::vector<uint8_t> single_byte_der = {0x30};
  auto single_der_public = impl->importPublicKeyDER(single_byte_der);
  EXPECT_EQ(nullptr, single_der_public->getEVP_PKEY()) << "Single byte DER public key should fail";

  auto single_der_private = impl->importPrivateKeyDER(single_byte_der);
  EXPECT_EQ(nullptr, single_der_private->getEVP_PKEY())
      << "Single byte DER private key should fail";
}

} // namespace
} // namespace Crypto
} // namespace Common
} // namespace Envoy
