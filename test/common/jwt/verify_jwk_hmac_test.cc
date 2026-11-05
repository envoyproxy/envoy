// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/verify.h"

#include "test/common/jwt/test_common.h"
#include "test/test_common/test_runtime.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

const std::string SymmetricKeyHMAC = R"(
{
  "keys": [
    {
      "kty": "oct",
      "alg": "HS256",
      "use": "sig",
      "kid": "62a93512c9ee4c7f8067b5a216dade2763d32a47",
      "k": "LcHQCLETtc_QO4D69zCnQEIAYaZ6BsldibDzuRHE5bI"
    },
    {
      "kty": "oct",
      "alg": "HS256",
      "use": "sig",
      "kid": "b3319a147514df7ee5e4bcdee51350cc890cc89e",
      "k": "nyeGXUHngW64dyg2EuDs_8x6VGa14Bkrv1SFQwOzKfI"
    },
    {
      "kty": "oct",
      "alg": "HS384",
      "use": "sig",
      "kid": "cda01077a6aa4b0088a6e959044977ef9e51c28b",
      "k": "5xYkMHiMVnCBbFEt0Uh1LhIbFB6yakzp2Mh7ESBMUCDq4zMO6WgCMaQwP332FH47"
    },
    {
      "kty": "oct",
      "alg": "HS512",
      "use": "sig",
      "kid": "f6a7bd9ffd784388924f126280a746964ba61268",
      "k": "ID3awf7bo607gitUDWylMMhUyVFr4ZAmnysPw4675A1YmOaYajbqLmMA7fohGLYZdZyaluaiugKvnnGLYTDoUA"
    },

  ]
}
)";

// JWT without kid
// Header:  {"alg":"HS256","typ":"JWT"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
const std::string JwtTextNoKid =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "_LY8Zz3ssG82v5-T8L2Hg1TsqzCEEKnYOxzrQpDTjwU";

// JWT without kid with long exp
// Header:  {"alg":"HS256","typ":"JWT"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","aud":"example_service","exp":2001001001}
const std::string JwtTextNoKidLongExp =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImF1ZCI6ImV4YW1wbGVfc2VydmljZSIsImV4cCI6MjAwMTAwMTAwMX0."
    "4tc7M-gJizpbB69_sQi7E0ym0np6uon4V41hVjYV2ic";

// JWT with correct kid
// Header:
// {"alg":"HS256","typ":"JWT","kid":"b3319a147514df7ee5e4bcdee51350cc890cc89e"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
const std::string JwtHS256TextWithCorrectKid =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImIzMzE5YTE0NzUxNGRmN2VlNWU0"
    "YmNkZWU1MTM1MGNjODkwY2M4OWUifQ."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "QqSMCAY5UDBvySx0VQhGqIvomZaSRUJOCT6ktV3BhL8";

// JWT with correct kid
// Header:
// {"alg":"HS384","typ":"JWT","kid":"cda01077a6aa4b0088a6e959044977ef9e51c28b"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
const std::string JwtHS384TextWithCorrectKid =
    "eyJhbGciOiJIUzM4NCIsInR5cCI6IkpXVCIsImtpZCI6ImNkYTAxMDc3YTZhYTRiMDA4OGE2"
    "ZTk1OTA0NDk3N2VmOWU1MWMyOGIifQ."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "F69ivpIRbgrmy1j6_MHl10xDW8iPdzsHAIgln3Z9PEemH9heiQoDUOgG91kA44fL";

// JWT with correct kid
// Header:
// {"alg":"HS512","typ":"JWT","kid":"f6a7bd9ffd784388924f126280a746964ba61268"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
const std::string JwtHS512TextWithCorrectKid =
    "eyJhbGciOiJIUzUxMiIsInR5cCI6IkpXVCIsImtpZCI6ImY2YTdiZDlmZmQ3ODQzODg5MjRm"
    "MTI2MjgwYTc0Njk2NGJhNjEyNjgifQ."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "YdILUM4zaeIRuxEMLV13qMX3d1sp63juPXwbpOp_HUjNdGGvocthipOxjQur6JtCLmIfvrI4"
    "XNrkxVWd-qS_3g";

// JWT with existing but incorrect kid
// Header:
// {"alg":"HS256","typ":"JWT","kid":"62a93512c9ee4c7f8067b5a216dade2763d32a47"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
const std::string JwtTextWithIncorrectKid =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6IjYyYTkzNTEyYzllZTRjN2Y4MDY3"
    "YjVhMjE2ZGFkZTI3NjNkMzJhNDcifQ."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "GRLODq7HrBduwUJEoJ3alWlXvxhCZZpFgvd1hYRDXa4";

// JWT with non-existent kid
// Header:  {"alg":"HS256","typ":"JWT","kid":"blahblahblah"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
const std::string JwtTextWithNonExistKid =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImJsYWhibGFoYmxhaCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "WFHsFo29tA5_gT_rzm6WheQhCwwBPrRZWFEAWRF9Ym4";

// Two HS256 keys without kid, identified by certificate thumbprint. JWKS keys are tried in order,
// so a token signed by the second key verifies only if the first is skipped or fails.
const std::string SymmetricKeyHMACWithThumbprints = R"(
{
  "keys": [
    {
      "kty": "oct",
      "alg": "HS256",
      "kid": "key-a",
      "x5t#S256": "thumb-s256-a",
      "x5t": "thumb-a",
      "k": "LcHQCLETtc_QO4D69zCnQEIAYaZ6BsldibDzuRHE5bI"
    },
    {
      "kty": "oct",
      "alg": "HS256",
      "kid": "key-b",
      "x5t#S256": "thumb-s256-b",
      "x5t": "thumb-b",
      "k": "nyeGXUHngW64dyg2EuDs_8x6VGa14Bkrv1SFQwOzKfI"
    }
  ]
}
)";

// All tokens below have payload
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
// and no kid unless the header says otherwise.

// Header:  {"alg":"HS256","typ":"JWT","x5t#S256":"thumb-s256-b"}
const std::string JwtX5tS256B =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCIsIng1dCNTMjU2IjoidGh1bWItczI1Ni1iIn0."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "ldFlvSOUNVP8Ysp1pit7qq3vZ54AZc4_KucEO_kgQGI";

// Header:  {"alg":"HS256","typ":"JWT","x5t#S256":"thumb-s256-b"}
const std::string JwtX5tS256BSignedByA =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCIsIng1dCNTMjU2IjoidGh1bWItczI1Ni1iIn0."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "Z638Q0mWPV8uJAsWQakRFL3nBtmculcDpzM0ECT5m28";

// Header:  {"alg":"HS256","typ":"JWT","x5t":"thumb-b"}
const std::string JwtX5tB =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCIsIng1dCI6InRodW1iLWIifQ."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "vQfhpi14j9gWz8NHjwvMuOuOF5_8vRk5_an_WBO6ebY";

// Header:  {"alg":"HS256","typ":"JWT","x5t":"thumb-b"}
const std::string JwtX5tBSignedByA =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCIsIng1dCI6InRodW1iLWIifQ."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "0TRzXBCW7037ByvUwwAZz8t6QBQgO5UIJj4DEtV_VlI";

// Header:  {"alg":"HS256","typ":"JWT","x5t#S256":"thumb-s256-b","x5t":"thumb-a"}
const std::string JwtX5tS256BX5tA =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCIsIng1dCNTMjU2IjoidGh1bWItczI1Ni1iIiwieDV0IjoidGh1bWItYSJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "g0_Va-2izYzo3toqXO26AxhgScDdFqW9BDaIouJU4s8";

// Header:  {"alg":"HS256","typ":"JWT","x5t#S256":"thumb-unknown"}
const std::string JwtX5tUnknown =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCIsIng1dCNTMjU2IjoidGh1bWItdW5rbm93biJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "HRXbkIliqOs6oSYwnjG-YA1ucx7rm4qk2s2dJdTq3yc";

// Header:  {"alg":"HS256","typ":"JWT","kid":"key-b","x5t#S256":"thumb-s256-a"}
const std::string JwtKidBX5tA =
    "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImtleS1iIiwieDV0I1MyNTYiOiJ0aHVtYi1zMjU2LWEifQ."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "YGeTnzN4YWtVo4nZISFuzTuW5FmVROIkQwwKO_Tu4dk";

class VerifyJwkHmacTest : public testing::Test {
protected:
  void SetUp() override {
    jwks_ = Jwks::createFrom(SymmetricKeyHMAC, Jwks::Type::JWKS);
    EXPECT_EQ(jwks_->getStatus(), Status::Ok);
  }

  JwksPtr jwks_;
};

TEST_F(VerifyJwkHmacTest, NoKidOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextNoKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);
  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

TEST_F(VerifyJwkHmacTest, NoKidLongExpOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextNoKidLongExp), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_), Status::Ok);

  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

TEST_F(VerifyJwkHmacTest, CorrectKidHS256OK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtHS256TextWithCorrectKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);

  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

TEST_F(VerifyJwkHmacTest, CorrectKidHS384OK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtHS384TextWithCorrectKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);

  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

TEST_F(VerifyJwkHmacTest, CorrectKidHS512OK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtHS512TextWithCorrectKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);

  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

TEST_F(VerifyJwkHmacTest, NonExistKidFail) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextWithNonExistKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwksKidAlgMismatch);
}

TEST_F(VerifyJwkHmacTest, OkSymmetricKeyNotAlg) {
  // Remove "alg" claim from symmetric key.
  std::string alg_claim = R"("alg": "HS256",)";
  std::string symmkey_no_alg = SymmetricKeyHMAC;
  std::size_t alg_pos = symmkey_no_alg.find(alg_claim);
  while (alg_pos != std::string::npos) {
    symmkey_no_alg.erase(alg_pos, alg_claim.length());
    alg_pos = symmkey_no_alg.find(alg_claim);
  }

  jwks_ = Jwks::createFrom(symmkey_no_alg, Jwks::Type::JWKS);
  EXPECT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextNoKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);
}

TEST_F(VerifyJwkHmacTest, OkSymmetricKeyNotKid) {
  // Remove "kid" claim from symmetric key.
  std::string kid_claim1 = R"("kid": "62a93512c9ee4c7f8067b5a216dade2763d32a47",)";
  std::string kid_claim2 = R"("kid": "b3319a147514df7ee5e4bcdee51350cc890cc89e",)";
  std::string symmkey_no_kid = SymmetricKeyHMAC;
  std::size_t kid_pos = symmkey_no_kid.find(kid_claim1);
  symmkey_no_kid.erase(kid_pos, kid_claim1.length());
  kid_pos = symmkey_no_kid.find(kid_claim2);
  symmkey_no_kid.erase(kid_pos, kid_claim2.length());
  jwks_ = Jwks::createFrom(symmkey_no_kid, Jwks::Type::JWKS);
  EXPECT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextNoKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);
}

TEST_F(VerifyJwkHmacTest, X5tS256SelectsKey) {
  jwks_ = Jwks::createFrom(SymmetricKeyHMACWithThumbprints, Jwks::Type::JWKS);
  ASSERT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtX5tS256B), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);

  // Key A is skipped, so a token it signed fails against key B.
  EXPECT_EQ(jwt.parseFromString(JwtX5tS256BSignedByA), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
}

TEST_F(VerifyJwkHmacTest, X5tIgnoredWithRuntimeGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.jwt_authn_select_key_by_x5t", "false"}});
  jwks_ = Jwks::createFrom(SymmetricKeyHMACWithThumbprints, Jwks::Type::JWKS);
  ASSERT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtX5tS256BSignedByA), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);
}

TEST_F(VerifyJwkHmacTest, X5tSelectsKey) {
  jwks_ = Jwks::createFrom(SymmetricKeyHMACWithThumbprints, Jwks::Type::JWKS);
  ASSERT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtX5tB), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);

  EXPECT_EQ(jwt.parseFromString(JwtX5tBSignedByA), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
}

TEST_F(VerifyJwkHmacTest, X5tS256TakesPrecedenceOverX5t) {
  jwks_ = Jwks::createFrom(SymmetricKeyHMACWithThumbprints, Jwks::Type::JWKS);
  ASSERT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtX5tS256BX5tA), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);
}

TEST_F(VerifyJwkHmacTest, X5tUnknownFail) {
  jwks_ = Jwks::createFrom(SymmetricKeyHMACWithThumbprints, Jwks::Type::JWKS);
  ASSERT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtX5tUnknown), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwksKidAlgMismatch);
}

TEST_F(VerifyJwkHmacTest, KidTakesPrecedenceOverX5t) {
  jwks_ = Jwks::createFrom(SymmetricKeyHMACWithThumbprints, Jwks::Type::JWKS);
  ASSERT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtKidBX5tA), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);
}

TEST_F(VerifyJwkHmacTest, X5tKeyWithoutThumbprintIsTried) {
  // The default key set has no thumbprints, so every key is still tried.
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtX5tS256B), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy
