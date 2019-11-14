#include "common/buffer/buffer_impl.h"
#include "common/common/hex.h"
#include "common/crypto/utility.h"

#include "extensions/common/crypto/crypto_impl.h"

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
}

TEST(UtilityTest, TestVerifySignature) {
  auto key = "30820122300d06092a864886f70d01010105000382010f003082010a0282010100a7471266d01d160308d"
             "73409c06f2e8d35c531c458d3e480e9f3191847d062ec5ccff7bc51e949d5f2c3540c189a4eca1e8633a6"
             "2cf2d0923101c27e38013e71de9ae91a704849bff7fbe2ce5bf4bd666fd9731102a53193fe5a9a5a50644"
             "ff8b1183fa897646598caad22a37f9544510836372b44c58c98586fb7144629cd8c9479592d996d32ff6d"
             "395c0b8442ec5aa1ef8051529ea0e375883cefc72c04e360b4ef8f5760650589ca814918f678eee39b884"
             "d5af8136a9630a6cc0cde157dc8e00f39540628d5f335b2c36c54c7c8bc3738a6b21acff815405afa28e5"
             "183f550dac19abcf1145a7f9ced987db680e4a229cac75dee347ec9ebce1fc3dbbbb0203010001";
  auto hash_func = "sha256";
  auto signature =
      "345ac3a167558f4f387a81c2d64234d901a7ceaa544db779d2f797b0ea4ef851b740905a63e2f4d5af42cee093a2"
      "9c7155db9a63d3d483e0ef948f5ac51ce4e10a3a6606fd93ef68ee47b30c37491103039459122f78e1c7ea71a1a5"
      "ea24bb6519bca02c8c9915fe8be24927c91812a13db72dbcb500103a79e8f67ff8cb9e2a631974e0668ab3977bf5"
      "70a91b67d1b6bcd5dce84055f21427d64f4256a042ab1dc8e925d53a769f6681a873f5859693a7728fcbe95beace"
      "1563b5ffbcd7c93b898aeba31421dafbfadeea50229c49fd6c445449314460f3d19150bd29a91333beaced557ed6"
      "295234f7c14fa46303b7e977d2c89ba8a39a46a35f33eb07a332";
  auto data = "hello";

  Common::Crypto::CryptoObjectPtr crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPublicKey(Hex::decode(key)));
  Common::Crypto::CryptoObject* crypto(crypto_ptr.get());

  std::vector<uint8_t> text(data, data + strlen(data));

  auto sig = Hex::decode(signature);
  auto result = UtilitySingleton::get().verifySignature(hash_func, *crypto, sig, text);

  EXPECT_EQ(true, result.result_);
  EXPECT_EQ("", result.error_message_);

  result = UtilitySingleton::get().verifySignature("unknown", *crypto, sig, text);
  EXPECT_EQ(false, result.result_);
  EXPECT_EQ("unknown is not supported.", result.error_message_);

  PublicKeyObject* empty_crypto = new PublicKeyObject();
  result = UtilitySingleton::get().verifySignature(hash_func, *empty_crypto, sig, text);
  EXPECT_EQ(false, result.result_);
  EXPECT_EQ("Failed to initialize digest verify.", result.error_message_);

  data = "baddata";
  text = std::vector<uint8_t>(data, data + strlen(data));
  result = UtilitySingleton::get().verifySignature(hash_func, *crypto, sig, text);
  EXPECT_EQ(false, result.result_);
  EXPECT_EQ("Failed to verify digest. Error code: 0", result.error_message_);

  data = "hello";
  text = std::vector<uint8_t>(data, data + strlen(data));
  result = UtilitySingleton::get().verifySignature(hash_func, *crypto, Hex::decode("000000"), text);
  EXPECT_EQ(false, result.result_);
  EXPECT_EQ("Failed to verify digest. Error code: 0", result.error_message_);
}

} // namespace
} // namespace Crypto
} // namespace Common
} // namespace Envoy
