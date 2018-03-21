#include "common/jwt_authn/jwt.h"

#include "test/test_common/utility.h"

namespace Envoy {
namespace JwtAuthn {
namespace {

TEST(JwtParseTest, GoodJwt) {
  // JWT with
  // Header:  {"alg":"RS256","typ":"JWT"}
  // Payload:
  // {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
  const std::string kJwt =
      "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
      "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
      "ImV4cCI6MTUwMTI4MTA1OH0.U2lnbmF0dXJl";

  Jwt jwt;
  ASSERT_EQ(jwt.ParseFromString(kJwt), Status::OK);

  EXPECT_EQ(jwt.alg, "RS256");
  EXPECT_EQ(jwt.kid, "");
  EXPECT_EQ(jwt.iss, "https://example.com");
  EXPECT_EQ(jwt.sub, "test@example.com");
  EXPECT_EQ(jwt.aud, std::vector<std::string>());
  EXPECT_EQ(jwt.exp, 1501281058);
  EXPECT_EQ(jwt.signature, "Signature");
}

TEST(JwtParseTest, GoodJwtWithMultiAud) {
  // aud: [aud1, aud2]
  const std::string kJwt = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImFmMDZjMTlmOGU1YjMzMTUyMT"
                           "ZkZjAxMGZkMmI5YTkzYmFjMTM1YzgifQ.eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tI"
                           "iwiaWF0IjoxNTE3ODc1MDU5LCJhdWQiOlsiYXVkMSIsImF1ZDIiXSwiZXhwIjoxNTE3ODc"
                           "4NjU5LCJzdWIiOiJodHRwczovL2V4YW1wbGUuY29tIn0.U2lnbmF0dXJl";

  Jwt jwt;
  ASSERT_EQ(jwt.ParseFromString(kJwt), Status::OK);

  EXPECT_EQ(jwt.alg, "RS256");
  EXPECT_EQ(jwt.kid, "af06c19f8e5b3315216df010fd2b9a93bac135c8");
  EXPECT_EQ(jwt.iss, "https://example.com");
  EXPECT_EQ(jwt.sub, "https://example.com");
  EXPECT_EQ(jwt.aud, std::vector<std::string>({"aud1", "aud2"}));
  EXPECT_EQ(jwt.exp, 1517878659);
  EXPECT_EQ(jwt.signature, "Signature");
}

TEST(JwtParseTest, EmptyJwt) {
  Jwt jwt;
  ASSERT_EQ(jwt.ParseFromString(""), Status::JWT_BAD_FORMAT);
}

TEST(JwtParseTest, BadJsonHeader) {
  /*
   * jwt with header replaced by
   * "{"alg":"RS256","typ":"JWT", this is a invalid json}"
   */
  const std::string kJwtWithBadJsonHeader =
      "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsIHRoaXMgaXMgYSBpbnZhbGlkIGpzb259."
      "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
      "ImV4cCI6MTUwMTI4MTA1OH0.VGVzdFNpZ25hdHVyZQ";

  Jwt jwt;
  ASSERT_EQ(jwt.ParseFromString(kJwtWithBadJsonHeader), Status::JWT_HEADER_PARSE_ERROR);
}

TEST(JwtParseTest, BadJsonPayload) {
  /*
   * jwt with payload replaced by
   * "this is not a json"
   */
  const std::string kJwtWithBadJsonPayload =
      "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.dGhpcyBpcyBub3QgYSBqc29u."
      "VGVzdFNpZ25hdHVyZQ";

  Jwt jwt;
  ASSERT_EQ(jwt.ParseFromString(kJwtWithBadJsonPayload), Status::JWT_PAYLOAD_PARSE_ERROR);
}

TEST(JwtParseTest, AbsentAlg) {
  /*
   * jwt with header replaced by
   * "{"typ":"JWT"}"
   */
  const std::string kJwtWithAlgAbsent =
      "eyJ0eXAiOiJKV1QifQ."
      "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
      "ImV4cCI6MTUwMTI4MTA1OH0"
      ".VGVzdFNpZ25hdHVyZQ";

  Jwt jwt;
  ASSERT_EQ(jwt.ParseFromString(kJwtWithAlgAbsent), Status::JWT_HEADER_NO_ALG);
}

TEST(JwtParseTest, AlgIsNotString) {
  /*
   * jwt with header replaced by
   * "{"alg":256,"typ":"JWT"}"
   */
  const std::string kJwtWithAlgIsNotString =
      "eyJhbGciOjI1NiwidHlwIjoiSldUIn0."
      "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
      "ImV4cCI6MTUwMTI4MTA1OH0.VGVzdFNpZ25hdHVyZQ";

  Jwt jwt;
  ASSERT_EQ(jwt.ParseFromString(kJwtWithAlgIsNotString), Status::JWT_HEADER_BAD_ALG);
}

TEST(JwtParseTest, InvalidAlg) {
  /*
   * jwt with header replaced by
   * "{"alg":"InvalidAlg","typ":"JWT"}"
   */
  const std::string kJwtWithInvalidAlg =
      "eyJhbGciOiJJbnZhbGlkQWxnIiwidHlwIjoiSldUIn0."
      "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
      "ImV4cCI6MTUwMTI4MTA1OH0.VGVzdFNpZ25hdHVyZQ";

  Jwt jwt;
  ASSERT_EQ(jwt.ParseFromString(kJwtWithInvalidAlg), Status::ALG_NOT_IMPLEMENTED);
}

TEST(JwtParseTest, BadFormatKid) {
  // JWT with bad-formatted kid
  // Header:  {"alg":"RS256","typ":"JWT","kid":1}
  // Payload:
  // {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
  const std::string kJwtWithBadFormatKid =
      "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6MX0."
      "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
      "ImV4cCI6MTUwMTI4MTA1OH0.VGVzdFNpZ25hdHVyZQ";

  Jwt jwt;
  ASSERT_EQ(jwt.ParseFromString(kJwtWithBadFormatKid), Status::JWT_HEADER_BAD_KID);
}

TEST(JwtParseTest, InvalidSignature) {
  // {"iss":"https://example.com","sub":"test@example.com","exp":1501281058,
  // aud: [aud1, aud2] }
  // signature part is invalid.
  const std::string kJwtInvalidSignature =
      "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImFmMDZjMTlmOGU1YjMzMTUyMT"
      "ZkZjAxMGZkMmI5YTkzYmFjMTM1YzgifQ.eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tI"
      "iwiaWF0IjoxNTE3ODc1MDU5LCJhdWQiOlsiYXVkMSIsImF1ZDIiXSwiZXhwIjoxNTE3ODc"
      "4NjU5LCJzdWIiOiJodHRwczovL2V4YW1wbGUuY29tIn0.invalid-signature";

  Jwt jwt;
  ASSERT_EQ(jwt.ParseFromString(kJwtInvalidSignature), Status::JWT_SIGNATURE_PARSE_ERROR);
}

} // namespace
} // namespace JwtAuthn
} // namespace Envoy
