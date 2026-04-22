// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/verify.h"

#include "test/common/jwt/test_common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

// To generate new keys:
// ``$ openssl genpkey -algorithm ed25519 -out ed_private.pem``
// To get the "x" value of the public key:
// https://mta.openssl.org/pipermail/openssl-users/2018-March/007777.html
// tr is used to convert to the URL-safe, no padding form of Base64 used by JWKS
// $ openssl pkey -in ed_private.pem -pubout -outform DER | tail -c +13 | base64
// | tr '+/' '-_' | tr -d '='
// To generate new JWTs: I used https://pypi.org/project/privex-pyjwt/

// Ed25519 private key
// -----BEGIN PRIVATE KEY-----
// MC4CAQAwBQYDK2VwBCIEIHU2mIWEGpLJ4f6wz0+6DZOCpQ3c/HrqQP5i3LDi6BLe
// -----END PRIVATE KEY-----

// Ed25519 public key
// -----BEGIN PUBLIC KEY-----
// MCowBQYDK2VwAyEA6hH43mEbo+h7iigPm9zLKHH5oEc+bjIXD/t4PLPqHLQ=
// -----END PUBLIC KEY-----

const std::string PublicKeyJwkOKP = R"(
{
   "keys": [
      {
         "kty": "OKP",
         "crv": "Ed25519",
         "alg": "EdDSA",
         "kid": "abc",
         "x": "6hH43mEbo-h7iigPm9zLKHH5oEc-bjIXD_t4PLPqHLQ"
      },
      {
         "kty": "OKP",
         "crv": "Ed25519",
         "alg": "EdDSA",
         "kid": "xyz",
         "x": "6hH43mEbo-h7iigPm9zLKHH5oEc-bjIXD_t4PLPqHLQ"
      }
  ]
 }
)";

// Header:  ``{"alg": "EdDSA", "kid": "abc", "typ": "JWT"}``
// Payload: ``{"iss":"https://example.com", "sub":"test@example.com"}``
const std::string JwtJWKEd25519 =
    "eyJ0eXAiOiJKV1QiLCJhbGciOiJFZERTQSIsImtpZCI6ImFiYyJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "n7Jd_zwXE03FFDrjdxDP3CYJqAlFXCa3jbv8qER_Z5cmisGJ3_"
    "gEb2j1IALPtLA8TsYxQJ4Xxfucen9nFqxUBg";

// Header:  ``{"alg": "EdDSA", "typ": "JWT"}``
// Payload: ``{"iss":"https://example.com", "sub":"test@example.com"}``
const std::string JwtJWKEd25519NoKid =
    "eyJ0eXAiOiJKV1QiLCJhbGciOiJFZERTQSJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "rn-h5xTejtilHiAG6aKJEQ3e5_"
    "aIKC7nwKUPOjBqN8df69JLiFtKxFCDINHtCNhoeLkgcDHHo2SJFincVH_OCg";

// Header:  ``{"alg": "EdDSA", "kid": "abcdef", "typ": "JWT"}``
// Payload: ``{"iss":"https://example.com", "sub":"test@example.com"}``
const std::string JwtJWKEd25519NonExistKid =
    "eyJ0eXAiOiJKV1QiLCJhbGciOiJFZERTQSIsImtpZCI6ImFiY2RlZiJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "dqLWFow63rL9VFsjtea60hZn5wMZJxNM6pGcVcOEOE38HrkY1miLj2ZIavd8P7NkkqEsuZMkZ4"
    "QHcZxm8qRiCA";

// Header:  ``{"alg": "EdDSA", "kid": "abc", "typ": "JWT"}``
// Payload: ``{"iss":"https://example.com", "sub":"test@example.com"}``
// But signed by a different key
const std::string JwtJWKEd25519WrongSignature =
    "eyJ0eXAiOiJKV1QiLCJhbGciOiJFZERTQSIsImtpZCI6ImFiYyJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "Y-Podsv8NFAQX07NbXAm6O8jD7KdzMpulh-kgDbuT-AspA_"
    "tT7aebOM2GHb2q6qex1O6BFkp5n8-2wrwKKE1BQ";

class VerifyJwkOKPTest : public testing::Test {
protected:
  void SetUp() {
    jwks_ = Jwks::createFrom(PublicKeyJwkOKP, Jwks::Type::JWKS);
    EXPECT_EQ(jwks_->getStatus(), Status::Ok);
  }

  JwksPtr jwks_;
};

TEST_F(VerifyJwkOKPTest, KidOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtJWKEd25519), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_), Status::Ok);

  fuzzJwtSignatureBits(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
  fuzzJwtSignatureLength(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtEd25519SignatureWrongLength);
  });
}

TEST_F(VerifyJwkOKPTest, NoKidOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtJWKEd25519NoKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_), Status::Ok);

  fuzzJwtSignatureBits(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
  fuzzJwtSignatureLength(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtEd25519SignatureWrongLength);
  });
}

TEST_F(VerifyJwkOKPTest, NonExistKidFail) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtJWKEd25519NonExistKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwksKidAlgMismatch);
}

TEST_F(VerifyJwkOKPTest, PubkeyNoAlgOK) {
  // Remove "alg" claim from public key.
  std::string alg_claim = R"("alg": "EdDSA",)";
  std::string pubkey_no_alg = PublicKeyJwkOKP;
  std::size_t alg_pos = pubkey_no_alg.find(alg_claim);
  while (alg_pos != std::string::npos) {
    pubkey_no_alg.erase(alg_pos, alg_claim.length());
    alg_pos = pubkey_no_alg.find(alg_claim);
  }

  jwks_ = Jwks::createFrom(pubkey_no_alg, Jwks::Type::JWKS);
  EXPECT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtJWKEd25519), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);
}

TEST_F(VerifyJwkOKPTest, PubkeyNoKidOK) {
  // Remove "kid" claim from public key.
  std::string kid_claim1 = R"("kid": "abc",)";
  std::string kid_claim2 = R"("kid": "xyz",)";
  std::string pubkey_no_kid = PublicKeyJwkOKP;
  std::size_t kid_pos = pubkey_no_kid.find(kid_claim1);
  pubkey_no_kid.erase(kid_pos, kid_claim1.length());
  kid_pos = pubkey_no_kid.find(kid_claim2);
  pubkey_no_kid.erase(kid_pos, kid_claim2.length());

  jwks_ = Jwks::createFrom(pubkey_no_kid, Jwks::Type::JWKS);
  EXPECT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtJWKEd25519), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);
}

TEST_F(VerifyJwkOKPTest, WrongSignatureFail) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtJWKEd25519WrongSignature), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_), Status::JwtVerificationFail);
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy
