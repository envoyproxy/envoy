#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/base64.h"
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
}

TEST(UtilityTest, TestImportPrivateKey) {
  auto key =
      "30820274020100300D06092A864886F70D01010105000482025E3082025A02010002818076C5C24261612C013B0A"
      "BD92938A16E1856E874C1D74B37F09A944D53E8135E953B279656B230108FB52F159B625423264E336E43050A7DF"
      "C49AAA293A27FF458FE8B00EE9089796C1778FB323BD5FBA67137EABDF49BF7FA68D81C089C8867096B964136CEF"
      "D8C531992E2A06E1674F834516011EDC64528204169715B3FFE9020301000102818027E21A6C5DF4DA69036184ED"
      "0E7C2558CF8CA1042F33FBFE61C92463131D22745A75A90C2460D9BD215FE5C9C13F5BAE3E708A033032355D0FD0"
      "FBE8E22D822B6A55C14D3D6E21211A745F4F6BCAA59CA312C10839C4DC0C02FD2AECCA8E629A220143840C17E5E8"
      "37183B92B99E0742EE111E2AD2E084350323EB769BDCE30D024100DCDBFC39215B4694F22B9D1A9A2B40D12F8E81"
      "F80540A8275E1498AFFDED6C24C046945651CB0A251ADF113B8A8ED9716E729F700E0DA0801EC009763D1284EF02"
      "410089AB95EA7A44DC395C4D6195E24AD4C2D82978C3DF47E5E85476A798B3768958E4E4423F3BC9214F1A27471C"
      "B33255466C28DB309B6B244F01222B81B52538A702403B656A128F36F5E76EAD6E05CE7A5D67248C05C606DB999D"
      "64BED345595BF59E789B429F6845DB87990F6E99FDAC672C0B510631E385A4A9701BA32FCA42E5BF0240120C7CCB"
      "10DC9642AEE736340046EF3DDC3913AC1A49C2CA82C84B90A97690EB2697065863EE2A7FC45E01E4B15997F47399"
      "A7A2E7BD54354760C3736DDC436102407C3C60AFD6E862BAD626BDF43C189BBB1C50D03932B5B567131EEDB0D06B"
      "2DD4EFC81CA07C755FE2687D8B13A05015A4EAA82DF2844CA3C4579A6B08B231A95F";

  Common::Crypto::CryptoObjectPtr crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPrivateKey(Hex::decode(key)));
  auto wrapper = Common::Crypto::Access::getTyped<Common::Crypto::PrivateKeyObject>(*crypto_ptr);
  EVP_PKEY* pkey = wrapper->getEVP_PKEY();
  EXPECT_NE(nullptr, pkey);

  key = "badkey";
  crypto_ptr = Common::Crypto::UtilitySingleton::get().importPrivateKey(Hex::decode(key));
  wrapper = Common::Crypto::Access::getTyped<Common::Crypto::PrivateKeyObject>(*crypto_ptr);
  pkey = wrapper->getEVP_PKEY();
  EXPECT_EQ(nullptr, pkey);
}

TEST(UtilityTest, TestEncrypt) {
  auto key = "30820122300d06092a864886f70d01010105000382010f003082010a0282010100a7471266d01d160308d"
             "73409c06f2e8d35c531c458d3e480e9f3191847d062ec5ccff7bc51e949d5f2c3540c189a4eca1e8633a6"
             "2cf2d0923101c27e38013e71de9ae91a704849bff7fbe2ce5bf4bd666fd9731102a53193fe5a9a5a50644"
             "ff8b1183fa897646598caad22a37f9544510836372b44c58c98586fb7144629cd8c9479592d996d32ff6d"
             "395c0b8442ec5aa1ef8051529ea0e375883cefc72c04e360b4ef8f5760650589ca814918f678eee39b884"
             "d5af8136a9630a6cc0cde157dc8e00f39540628d5f335b2c36c54c7c8bc3738a6b21acff815405afa28e5"
             "183f550dac19abcf1145a7f9ced987db680e4a229cac75dee347ec9ebce1fc3dbbbb0203010001";
  std::string data = "hello";

  Common::Crypto::CryptoObjectPtr crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPublicKey(Hex::decode(key)));
  Common::Crypto::CryptoObject* crypto(crypto_ptr.get());

  std::vector<uint8_t> text(data.begin(), data.end());

  auto result = UtilitySingleton::get().encrypt(*crypto, text);
  EXPECT_EQ(true, result.result_);

  auto empty_crypto = std::make_unique<PublicKeyObject>();
  result = UtilitySingleton::get().encrypt(*empty_crypto, text);
  EXPECT_EQ(false, result.result_);
  EXPECT_EQ("failed to get public key for encryption", result.data_);
}

TEST(UtilityTest, TestDecrypt) {
  auto key =
      "30820274020100300D06092A864886F70D01010105000482025E3082025A02010002818076C5C24261612C013B0A"
      "BD92938A16E1856E874C1D74B37F09A944D53E8135E953B279656B230108FB52F159B625423264E336E43050A7DF"
      "C49AAA293A27FF458FE8B00EE9089796C1778FB323BD5FBA67137EABDF49BF7FA68D81C089C8867096B964136CEF"
      "D8C531992E2A06E1674F834516011EDC64528204169715B3FFE9020301000102818027E21A6C5DF4DA69036184ED"
      "0E7C2558CF8CA1042F33FBFE61C92463131D22745A75A90C2460D9BD215FE5C9C13F5BAE3E708A033032355D0FD0"
      "FBE8E22D822B6A55C14D3D6E21211A745F4F6BCAA59CA312C10839C4DC0C02FD2AECCA8E629A220143840C17E5E8"
      "37183B92B99E0742EE111E2AD2E084350323EB769BDCE30D024100DCDBFC39215B4694F22B9D1A9A2B40D12F8E81"
      "F80540A8275E1498AFFDED6C24C046945651CB0A251ADF113B8A8ED9716E729F700E0DA0801EC009763D1284EF02"
      "410089AB95EA7A44DC395C4D6195E24AD4C2D82978C3DF47E5E85476A798B3768958E4E4423F3BC9214F1A27471C"
      "B33255466C28DB309B6B244F01222B81B52538A702403B656A128F36F5E76EAD6E05CE7A5D67248C05C606DB999D"
      "64BED345595BF59E789B429F6845DB87990F6E99FDAC672C0B510631E385A4A9701BA32FCA42E5BF0240120C7CCB"
      "10DC9642AEE736340046EF3DDC3913AC1A49C2CA82C84B90A97690EB2697065863EE2A7FC45E01E4B15997F47399"
      "A7A2E7BD54354760C3736DDC436102407C3C60AFD6E862BAD626BDF43C189BBB1C50D03932B5B567131EEDB0D06B"
      "2DD4EFC81CA07C755FE2687D8B13A05015A4EAA82DF2844CA3C4579A6B08B231A95F";
  auto data = Envoy::Base64::decode(
      "VjZ2GqLl6BBpFgbUkXBfwF5cNqOlsfORzIzrUVJalEpgv4QILBNVsiE7JHj47dlOSVZXY5MpfDEb1rVngmDYf1QsCtFp"
      "v/HzbHGNeS3qGEKgmL6SWObVD6ikBDsm3X4l+cyiTetu1KGXWGcF+gijWDhcTfaWAPuH2lugRQpJFWo=");

  Common::Crypto::CryptoObjectPtr crypto_ptr(
      Common::Crypto::UtilitySingleton::get().importPrivateKey(Hex::decode(key)));
  Common::Crypto::CryptoObject* crypto(crypto_ptr.get());

  std::vector<uint8_t> text(data.begin(), data.end());

  auto result = UtilitySingleton::get().decrypt(*crypto, text);

  EXPECT_EQ(true, result.result_);
  EXPECT_EQ("Hello, World!", result.data_);

  auto empty_crypto = std::make_unique<PrivateKeyObject>();
  result = UtilitySingleton::get().decrypt(*empty_crypto, text);
  EXPECT_EQ(false, result.result_);
  EXPECT_EQ("failed to get private key for decryption", result.data_);

  data = "";
  text = std::vector<uint8_t>(data.begin(), data.end());
  result = UtilitySingleton::get().decrypt(*crypto, text);
  EXPECT_EQ(false, result.result_);
  EXPECT_EQ("decryption failed", result.data_);
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

  auto empty_crypto = std::make_unique<PublicKeyObject>();
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
