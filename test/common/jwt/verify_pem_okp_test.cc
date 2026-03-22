// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#ifndef BORINGSSL_FIPS

#include "source/common/jwt/verify.h"

#include "test/common/jwt/test_common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

// SPELLCHECKER(off)
// To generate new keys:
// $ openssl genpkey -algorithm ed25519 -out ed_private.pem
// $ openssl pkey -in ed_private.pem -pubout -out ed_public.pem
// To generate new JWTs: I used https://pypi.org/project/privex-pyjwt/

// ED25519 private key:
// "-----BEGIN PRIVATE KEY-----"
// "MC4CAQAwBQYDK2VwBCIEIHU2mIWEGpLJ4f6wz0+6DZOCpQ3c/HrqQP5i3LDi6BLe"
// "-----END PRIVATE KEY-----"
// SPELLCHECKER(on)

const std::string ed25519pubkey = R"(
-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEA6hH43mEbo+h7iigPm9zLKHH5oEc+bjIXD/t4PLPqHLQ=
-----END PUBLIC KEY-----
)";

// SPELLCHECKER(off)
// JWT with
// Header:  {"alg": "EdDSA", "typ": "JWT"}
// Payload: {"iss":"https://example.com", "sub":"test@example.com"}
// SPELLCHECKER(on)
const std::string JwtPemEd25519 =
    "eyJ0eXAiOiJKV1QiLCJhbGciOiJFZERTQSJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "rn-h5xTejtilHiAG6aKJEQ3e5_"
    "aIKC7nwKUPOjBqN8df69JLiFtKxFCDINHtCNhoeLkgcDHHo2SJFincVH_OCg";

// SPELLCHECKER(off)
// Header:  {"alg": "EdDSA", typ": "JWT"}
// Payload: {"iss":"https://example.com", "sub":"test@example.com"}
// But signed by a different key
// SPELLCHECKER(on)
const std::string JwtPemEd25519WrongSignature =
    "eyJ0eXAiOiJKV1QiLCJhbGciOiJFZERTQSJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "Ob8ljAEmEwAoJFEf_v0YZozGlLLPCLVL2C-6B20S8tVNHTzL1-"
    "ZiFENdpY53gGakwJ7mm7aLYFikPKUQ62bYCg";

TEST(VerifyPEMTestOKP, VerifyOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemEd25519), Status::Ok);
  auto jwks = Jwks::createFrom(ed25519pubkey, Jwks::Type::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::Ok);

  fuzzJwtSignatureBits(jwt, [&jwks](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwtVerificationFail);
  });
  fuzzJwtSignatureLength(jwt, [&jwks](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwtEd25519SignatureWrongLength);
  });
}

TEST(VerifyPEMTestOKP, jwksIncorrectAlgSpecifiedFail) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemEd25519), Status::Ok);
  auto jwks = Jwks::createFrom(ed25519pubkey, Jwks::Type::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  // Add incorrect alg to jwks
  jwks->keys()[0]->alg_ = "RS256";
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwksKidAlgMismatch);
}

TEST(VerifyPEMTestOKP, WrongSignatureFail) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemEd25519WrongSignature), Status::Ok);
  auto jwks = Jwks::createFrom(ed25519pubkey, Jwks::Type::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwtVerificationFail);
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy

#endif
