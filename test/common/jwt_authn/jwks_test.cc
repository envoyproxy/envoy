#include "common/jwt_authn/jwks.h"
#include "test/test_common/utility.h"

namespace Envoy {
namespace JwtAuthn {
namespace {

TEST(JwksParseTest, GoodPem) {
  // Public key PEM
  const std::string kPublicKeyPEM =
      "MIIBCgKCAQEAtw7MNxUTxmzWROCD5BqJxmzT7xqc9KsnAjbXCoqEEHDx4WBlfcwk"
      "XHt9e/2+Uwi3Arz3FOMNKwGGlbr7clBY3utsjUs8BTF0kO/poAmSTdSuGeh2mSbc"
      "VHvmQ7X/kichWwx5Qj0Xj4REU3Gixu1gQIr3GATPAIULo5lj/ebOGAa+l0wIG80N"
      "zz1pBtTIUx68xs5ZGe7cIJ7E8n4pMX10eeuh36h+aossePeuHulYmjr4N0/1jG7a"
      "+hHYL6nqwOR3ej0VqCTLS0OloC0LuCpLV7CnSpwbp2Qg/c+MDzQ0TH8g8drIzR5h"
      "Fe9a3NlNRMXgUU5RqbLnR9zfXr7b9oEszQIDAQAB";

  auto jwks = Jwks::CreateFrom(kPublicKeyPEM, Jwks::PEM);
  EXPECT_EQ(jwks->GetStatus(), Status::OK);
  EXPECT_EQ(jwks->keys().size(), 1);
  EXPECT_TRUE(jwks->keys()[0]->pem_format);
}

TEST(JwksParseTest, EmptyPem) {
  auto jwks = Jwks::CreateFrom("", Jwks::PEM);
  EXPECT_EQ(jwks->GetStatus(), Status::PEM_PUBKEY_BAD_BASE64);
}

TEST(JwksParseTest, BadPem) {
  // U2lnbmF0dXJl is Base64 of "Signature"
  auto jwks = Jwks::CreateFrom("U2lnbmF0dXJl", Jwks::PEM);
  EXPECT_EQ(jwks->GetStatus(), Status::PEM_PUBKEY_PARSE_ERROR);
}

TEST(JwksParseTest, GoodJwks) {
  const std::string kPublicKeyRSA =
      "{\"keys\": [{\"kty\": \"RSA\",\"alg\": \"RS256\",\"use\": "
      "\"sig\",\"kid\": \"62a93512c9ee4c7f8067b5a216dade2763d32a47\",\"n\": "
      "\"0YWnm_eplO9BFtXszMRQNL5UtZ8HJdTH2jK7vjs4XdLkPW7YBkkm_"
      "2xNgcaVpkW0VT2l4mU3KftR-6s3Oa5Rnz5BrWEUkCTVVolR7VYksfqIB2I_"
      "x5yZHdOiomMTcm3DheUUCgbJRv5OKRnNqszA4xHn3tA3Ry8VO3X7BgKZYAUh9fyZTFLlkeAh"
      "0-"
      "bLK5zvqCmKW5QgDIXSxUTJxPjZCgfx1vmAfGqaJb-"
      "nvmrORXQ6L284c73DUL7mnt6wj3H6tVqPKA27j56N0TB1Hfx4ja6Slr8S4EB3F1luYhATa1P"
      "KU"
      "SH8mYDW11HolzZmTQpRoLV8ZoHbHEaTfqX_aYahIw\",\"e\": \"AQAB\"},{\"kty\": "
      "\"RSA\",\"alg\": \"RS256\",\"use\": \"sig\",\"kid\": "
      "\"b3319a147514df7ee5e4bcdee51350cc890cc89e\",\"n\": "
      "\"qDi7Tx4DhNvPQsl1ofxxc2ePQFcs-L0mXYo6TGS64CY_"
      "2WmOtvYlcLNZjhuddZVV2X88m0MfwaSA16wE-"
      "RiKM9hqo5EY8BPXj57CMiYAyiHuQPp1yayjMgoE1P2jvp4eqF-"
      "BTillGJt5W5RuXti9uqfMtCQdagB8EC3MNRuU_KdeLgBy3lS3oo4LOYd-"
      "74kRBVZbk2wnmmb7IhP9OoLc1-7-9qU1uhpDxmE6JwBau0mDSwMnYDS4G_ML17dC-"
      "ZDtLd1i24STUw39KH0pcSdfFbL2NtEZdNeam1DDdk0iUtJSPZliUHJBI_pj8M-2Mn_"
      "oA8jBuI8YKwBqYkZCN1I95Q\",\"e\": \"AQAB\"}]}";

  auto jwks = Jwks::CreateFrom(kPublicKeyRSA, Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::OK);
  EXPECT_EQ(jwks->keys().size(), 2);

  EXPECT_EQ(jwks->keys()[0]->alg, "RS256");
  EXPECT_EQ(jwks->keys()[0]->kid, "62a93512c9ee4c7f8067b5a216dade2763d32a47");
  EXPECT_TRUE(jwks->keys()[0]->alg_specified);
  EXPECT_TRUE(jwks->keys()[0]->kid_specified);
  EXPECT_FALSE(jwks->keys()[0]->pem_format);

  EXPECT_EQ(jwks->keys()[1]->alg, "RS256");
  EXPECT_EQ(jwks->keys()[1]->kid, "b3319a147514df7ee5e4bcdee51350cc890cc89e");
  EXPECT_TRUE(jwks->keys()[1]->alg_specified);
  EXPECT_TRUE(jwks->keys()[1]->kid_specified);
  EXPECT_FALSE(jwks->keys()[1]->pem_format);
}

TEST(JwksParseTest, GoodEC) {
  // Public key JwkEC
  const std::string kPublicKeyJwkEC = "{\"keys\": ["
                                      "{"
                                      "\"kty\": \"EC\","
                                      "\"crv\": \"P-256\","
                                      "\"x\": \"EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k\","
                                      "\"y\": \"92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8\","
                                      "\"alg\": \"ES256\","
                                      "\"kid\": \"abc\""
                                      "},"
                                      "{"
                                      "\"kty\": \"EC\","
                                      "\"crv\": \"P-256\","
                                      "\"x\": \"EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k\","
                                      "\"y\": \"92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8\","
                                      "\"alg\": \"ES256\","
                                      "\"kid\": \"xyz\""
                                      "}"
                                      "]}";
  auto jwks = Jwks::CreateFrom(kPublicKeyJwkEC, Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::OK);
  EXPECT_EQ(jwks->keys().size(), 2);

  EXPECT_EQ(jwks->keys()[0]->alg, "ES256");
  EXPECT_EQ(jwks->keys()[0]->kid, "abc");
  EXPECT_EQ(jwks->keys()[0]->kty, "EC");
  EXPECT_TRUE(jwks->keys()[0]->alg_specified);
  EXPECT_TRUE(jwks->keys()[0]->kid_specified);
  EXPECT_FALSE(jwks->keys()[0]->pem_format);

  EXPECT_EQ(jwks->keys()[1]->alg, "ES256");
  EXPECT_EQ(jwks->keys()[1]->kid, "xyz");
  EXPECT_EQ(jwks->keys()[1]->kty, "EC");
  EXPECT_TRUE(jwks->keys()[1]->alg_specified);
  EXPECT_TRUE(jwks->keys()[1]->kid_specified);
  EXPECT_FALSE(jwks->keys()[1]->pem_format);
}

TEST(JwksParseTest, EmptyJwks) {
  auto jwks = Jwks::CreateFrom("", Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::JWK_PARSE_ERROR);
}

TEST(JwksParseTest, JwksNoKeys) {
  auto jwks = Jwks::CreateFrom("{}", Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::JWK_NO_KEYS);
}

TEST(JwksParseTest, JwksWrongKeys) {
  auto jwks = Jwks::CreateFrom("{\"keys\": 123}", Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::JWK_BAD_KEYS);
}

TEST(JwksParseTest, JwksInvalidKty) {
  // Invalid kty field
  const std::string kJwks = "{\"keys\": ["
                            "{"
                            "\"kty\": \"XYZ\","
                            "\"crv\": \"P-256\","
                            "\"x\": \"EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k\","
                            "\"y\": \"92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8\","
                            "\"alg\": \"ES256\","
                            "\"kid\": \"abc\""
                            "}"
                            "]}";
  auto jwks = Jwks::CreateFrom(kJwks, Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::JWK_NO_VALID_PUBKEY);
}

TEST(JwksParseTest, JwksMismatchKty1) {
  // kty doesn't match with alg
  const std::string kJwks = "{\"keys\": ["
                            "{"
                            "\"kty\": \"RSA\","
                            "\"alg\": \"ES256\""
                            "}"
                            "]}";
  auto jwks = Jwks::CreateFrom(kJwks, Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::JWK_NO_VALID_PUBKEY);
}

TEST(JwksParseTest, JwksMismatchKty2) {
  // kty doesn't match with alg
  const std::string kJwks = "{\"keys\": ["
                            "{"
                            "\"kty\": \"EC\","
                            "\"alg\": \"RS256\""
                            "}"
                            "]}";
  auto jwks = Jwks::CreateFrom(kJwks, Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::JWK_NO_VALID_PUBKEY);
}

TEST(JwksParseTest, JwksECNoXY) {
  const std::string kJwks = "{\"keys\": ["
                            "{"
                            "\"kty\": \"EC\","
                            "\"alg\": \"ES256\""
                            "}"
                            "]}";
  auto jwks = Jwks::CreateFrom(kJwks, Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::JWK_NO_VALID_PUBKEY);
}

TEST(JwksParseTest, JwksRSANoNE) {
  const std::string kJwks = "{\"keys\": ["
                            "{"
                            "\"kty\": \"RSA\","
                            "\"alg\": \"RS256\""
                            "}"
                            "]}";
  auto jwks = Jwks::CreateFrom(kJwks, Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::JWK_NO_VALID_PUBKEY);
}

TEST(JwksParseTest, JwksECWrongXY) {
  const std::string kJwks = "{\"keys\": ["
                            "{"
                            "\"kty\": \"EC\","
                            "\"x\": \"EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k111\","
                            "\"y\": \"92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8111\","
                            "\"alg\": \"ES256\""
                            "}"
                            "]}";
  auto jwks = Jwks::CreateFrom(kJwks, Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::JWK_EC_PUBKEY_PARSE_ERROR);
}

TEST(JwksParseTest, JwksRSAWrongNE) {
  const std::string kJwks = "{\"keys\": ["
                            "{"
                            "\"kty\": \"RSA\","
                            "\"n\": \"EB54wykhS7YJFD6RYJNnwbW\","
                            "\"e\": \"92bCBTvMFQ8lKbS2MbgjT3YfmY\","
                            "\"alg\": \"RS256\""
                            "}"
                            "]}";
  auto jwks = Jwks::CreateFrom(kJwks, Jwks::JWKS);
  EXPECT_EQ(jwks->GetStatus(), Status::JWK_RSA_PUBKEY_PARSE_ERROR);
}

} // namespace
} // namespace JwtAuthn
} // namespace Envoy
