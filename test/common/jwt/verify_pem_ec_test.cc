// Copyright 2020 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/verify.h"

#include "test/common/jwt/test_common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

// SPELLCHECKER(off)
// To generate new keys:
// $ openssl ecparam -name ${CurveName} -genkey -noout -out ec_private.pem
// $ openssl ec -in ec_private.pem -pubout -out ec_public.pem
// To generate new JWTs: Use jwt.io with the generated private key.

// ES256 private key:
// "-----BEGIN EC PRIVATE KEY-----"
// "MHcCAQEEIOyf96eKdFeSFYeHiM09vGAylz+/auaXKEr+fBZssFsJoAoGCCqGSM49"
// "AwEHoUQDQgAEEB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5n3ZsIFO8wV"
// "DyUptLYxuCNPdh+Zijoec8QTa2wCpZQnDw=="
// "-----END EC PRIVATE KEY-----"
// SPELLCHECKER(on)

const std::string es256pubkey = R"(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEQ4x/MTt08crvf9NsENzTH+XT3QdI
HCLizGaWwk3uaY7jx93jqFGY5z1xlXe3zyPgEZATV3IjloAkT6uxN6A2YA==
-----END PUBLIC KEY-----
)";

// SPELLCHECKER(off)
// ES384 private key:
// -----BEGIN EC PRIVATE KEY-----
// MIGkAgEBBDDqSPe2gvdUVMQcCxpr60rScFgjEQZeCYvZRq3oyY9mECVMK7nuRjLx
// blWjf6DH9E+gBwYFK4EEACKhZANiAATJjwNZzJaWuv3cVOuxwjlh3PY0Lt6Z+gpg
// cktfZ2vdxKB/DQa7ECS5DmcEwmZVXmACfnBXER+SwM5r/O9IccaR5glR+XzLXXBi
// Q6UWMG32k4LDn5GV9mA85reluZSq7Fk=
// -----END EC PRIVATE KEY-----
// SPELLCHECKER(on)

const std::string es384pubkey = R"(
-----BEGIN PUBLIC KEY-----
MHYwEAYHKoZIzj0CAQYFK4EEACIDYgAEyY8DWcyWlrr93FTrscI5Ydz2NC7emfoK
YHJLX2dr3cSgfw0GuxAkuQ5nBMJmVV5gAn5wVxEfksDOa/zvSHHGkeYJUfl8y11w
YkOlFjBt9pOCw5+RlfZgPOa3pbmUquxZ
-----END PUBLIC KEY-----
)";

// SPELLCHECKER(off)
// ES512 private key:
// -----BEGIN EC PRIVATE KEY-----
// MIHcAgEBBEIBKlG7GPIoqQujJHwe21rnsZePySFyd45HPe3FeldgZQEHqcUiZgpb
// BgiuYMPHytEaohj1yC5gyOOsOfgsWY2qSsWgBwYFK4EEACOhgYkDgYYABAG4o4ns
// e68+7fv2Y/xOjqNDl3vQv/jAkg/jloqNeQE0Box/VqW1ozetmaq61P58CYqqsMem
// bGCoVHPydz0WjG3VQgAXFqWMIi6hUQDs8khoM8nl49e1nSGSKdPUH9tD3WZKEKJH
// /jdaGyfU/sbPfRYScu4mzVIZXPWhPiUhFRieLY58iQ==
// -----END EC PRIVATE KEY-----
// SPELLCHECKER(on)

const std::string es512pubkey = R"(
-----BEGIN PUBLIC KEY-----
MIGbMBAGByqGSM49AgEGBSuBBAAjA4GGAAQBuKOJ7HuvPu379mP8To6jQ5d70L/4
wJIP45aKjXkBNAaMf1altaM3rZmqutT+fAmKqrDHpmxgqFRz8nc9Foxt1UIAFxal
jCIuoVEA7PJIaDPJ5ePXtZ0hkinT1B/bQ91mShCiR/43Whsn1P7Gz30WEnLuJs1S
GVz1oT4lIRUYni2OfIk=
-----END PUBLIC KEY-----
)";

// SPELLCHECKER(off)
// JWT with
// Header:  { "alg": "ES256", "typ": "JWT" }
// Payload: {"iss":"https://example.com","sub":"test@example.com" }
// SPELLCHECKER(on)
const std::string JwtPemEs256 =
    "eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "P2Ru0jfQrm4YgaN5aown5uf-LhV6QX6o-9eQ2D6TjWkJ62LxbIOu6eUnDYyn1QOaC6m2wdb-"
    "7NhcWG9DDijhiw";

// SPELLCHECKER(off)
// JWT with
// Header:  { "alg": "ES384", "typ": "JWT" }
// Payload: {"iss":"https://example.com","sub":"test@example.com" }
// SPELLCHECKER(on)
const std::string JwtPemEs384 =
    "eyJhbGciOiJFUzM4NCIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "jE8oJhDNem-xMhmylecKVaYhHH_"
    "9qJsC3oPz0M35ECI5OHkSOmbnOKtZg1kKFGYzgHDcahq3w3WAD7jtp7TtZbcS8z7PjJvBYSk7r"
    "FlHNurxmqF8-f_A03w3F9Lr0rWO";

// SPELLCHECKER(off)
// JWT with
// Header:  { "alg": "ES512", "typ": "JWT" }
// Payload: {"iss":"https://example.com","sub":"test@example.com" }
// SPELLCHECKER(on)
const std::string JwtPemEs512 =
    "eyJhbGciOiJFUzUxMiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "AMkxbTVhrtnX0Ylc8hI0nQFQkRhExqaQccHNJLL9aQd_"
    "0wlcZ8GHcXOaeKz8krRjxYw2kjHxg3Ng5Xtt7O_2AWN6AJ2FZ_"
    "742UKCFsCtCfZFP58d7UoTN7yZ8D4kmRCnh0GefX7z97eBCmMGmbSkCb87yGuDvxd1QlKiva1k"
    "kMGHCldt";

TEST(VerifyPKCSTestRs256, OKPem) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemEs256), Status::Ok);
  auto jwks = Jwks::createFrom(es256pubkey, Jwks::Type::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  jwks->keys()[0]->alg_ = "ES256";
  jwks->keys()[0]->crv_ = "P-256";
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::Ok);
  fuzzJwtSignature(jwt, [&jwks](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwtVerificationFail);
  });
}

TEST(VerifyPKCSTestES384, OKPem) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemEs384), Status::Ok);
  auto jwks = Jwks::createFrom(es384pubkey, Jwks::Type::PEM);
  jwks->keys()[0]->alg_ = "ES384";
  jwks->keys()[0]->crv_ = "P-384";
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::Ok);
  fuzzJwtSignature(jwt, [&jwks](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwtVerificationFail);
  });
}

TEST(VerifyPKCSTestES512, OKPem) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemEs512), Status::Ok);
  auto jwks = Jwks::createFrom(es512pubkey, Jwks::Type::PEM);
  jwks->keys()[0]->alg_ = "ES512";
  jwks->keys()[0]->crv_ = "P-512";
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::Ok);
  fuzzJwtSignature(jwt, [&jwks](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwtVerificationFail);
  });
}

// If the JWKS does not specific crv or alg, it will be inferred from the JWT.
TEST(VerifyPKCSTestES384, ES384CurveUnspecifiedOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemEs384), Status::Ok);
  auto jwks = Jwks::createFrom(es384pubkey, Jwks::Type::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::Ok);
}

TEST(VerifyPKCSTestRs256, jwksAlgUnspecifiedDoesNotMatchJwtFail) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemEs256), Status::Ok);
  // Wrong public key, for a different algorithm.
  auto jwks = Jwks::createFrom(es384pubkey, Jwks::Type::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwtVerificationFail);
  fuzzJwtSignature(jwt, [&jwks](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwtVerificationFail);
  });
}

TEST(VerifyPKCSTestRs256, jwksIncorrectAlgSpecifiedFail) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemEs256), Status::Ok);
  auto jwks = Jwks::createFrom(es256pubkey, Jwks::Type::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  // Add incorrect Alg to jwks.
  jwks->keys()[0]->alg_ = "ES512";
  jwks->keys()[0]->crv_ = "P-512";
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwksKidAlgMismatch);
  fuzzJwtSignature(jwt, [&jwks](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwksKidAlgMismatch);
  });
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy
