// Copyright 2020 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/verify.h"

#include "test/common/jwt/test_common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

// To generate new keys:
// openssl req -x509 -nodes -newkey rsa:2048 -keyout rsa_private.pem -subj
// "/CN=unused"
// openssl rsa -in rsa_private.pem -pubout -out rsa_public.pem
// To generate new JWTs: Use jwt.io with the generated private key.

const std::string pubkey = R"(
-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAzgP9Xw2xvul2pZNjCpJD
/L16FmKH/zt53seeo2/eBKzcUs3nDO33aYdjsCAAFaQfXSAe0PfmwbytmH9RMHOJ
PUU2ApcEt63K5+3v5n+kqKfmym2lOebqpLgsdXIXvTsHYYy/10GGM+NPgyMUgU8q
JSaPOOA/ZJ1eWQTyfgJCPeIarzcTaf+eSD3CQaDDpi488RFc3O86pho5x3KTHSg4
CxHp0ua1RV2pNGJP1BqN0oX09Rgpjo7GE+ukpCMO7zOCwSeBjnqL/zdJ7pjo//u0
dhGpdbcejNZhl1NN+0q1eogwJPM295/7xRSW77mmcUI8W4oLDHLz1zxRoX9yK9xv
3wIDAQAB
-----END PUBLIC KEY-----
)";

// JWT with
// Header:  { "alg": "RS256", "typ": "JWT" }
// Payload: {"iss":"https://example.com","sub":"test@example.com" }
const std::string JwtPemRs256 =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "FFSVKKWsuhwHaZDW-nsKcVY0_hEAHvfk7PNa-zRES9IPrhZU-dtZghhhP4xDdDc-"
    "w9Phg6Zy70JFOYBc7nvsaz4uRQg6YDYv5PEJaUcUnL4tu1_IK5KzuGnxLHJMVt-7F6EK_"
    "HVbvTgmTp6mruC1gvKr3aY3t9u_FQ6mSaziNecIlh9MzZlJ7MVQQhb9A047lbUtxGueGk0l3f-"
    "Idcg9idyIiBTqQuOfT1La088e4aCLQo6rCdAUsyeKaIjZyZmh-xK0-"
    "YMdobCyMBdEbeN5KWKv9kdSac0HaWbDNn_WKgtkmyIIv5iyPbCuo4vaZWwEQ7NSNsnQDe_"
    "BciDrX3npcg";

// JWT with
// Header:  { "alg": "RS384", "typ": "JWT" }
// Payload: {"iss":"https://example.com", "sub":"test@example.com" }
const std::string JwtPemRs384 =
    "eyJhbGciOiJSUzM4NCIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "ec8gDlwNnT189m78UklZ239UcThdRUrlh3DICZcunjb0h6nRrn8xX1zhF9OWiDjIS6Cu7c6kzA"
    "OgWu2ZDNf7WSG0JjmcpLVw8W-Zxs0zs6ycxQETz5d_hxmV0kGNRF0nM1EC5DfhB_"
    "ByOVwRkaHcM-kpX6t_zvZoX_FGJTp51QzUeGHL1I3WxSVrsTBpBGY_qLGU0dEE9rXgLEEw5o_"
    "k05f92PTPBTwq7J3kUYzwxEI9dFb10q9wQYMn1lRL2-"
    "Tw0LpdYYKcE8TWVaoNHSAsQQqErMwggIrxW4bg7V66EUSzzFUO8etFs2NN0mWobBQYG7kaCLVS"
    "eHlbAyIQagmjMg";

// JWT with
// Header:  { "alg": "RS512", "typ": "JWT" }
// Payload: {"iss":"https://example.com", "sub":"test@example.com" }
const std::string JwtPemRs512 =
    "eyJhbGciOiJSUzUxMiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "daL48TAqnpXWRltqVZSSVXwRxTuaI1hL5FqdUKNuUUHgDP511EOb_"
    "DmsgajvwYs4EmrS2kDguhur0vDIV4RbW3EHMPz3ngMNbP56oMyXOaiXc4dbEGhJraxZ3Y7xh2f"
    "H_CNOiXkEuAJns6fCxKHk-"
    "Wl1fV36k4mmPFpuxiZqiuRCP6c6Vprt55HKmO3cipjR0wBGrQi07vBwe2uHcZ6R4I6klCgVchq"
    "Ms5qq2T1jSnLir6Z4YDgbw6L7lO_x9w2Rhw6R0impjDya2sBrQ-KdATaE5Zkyd5BU6L-"
    "IEqKrrJdVTr_rhBYMIMDjDk7ufioIY-6A0zBDQdM2xw3evwBE_w";

TEST(VerifyPKCSTestRs256, OKPem) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemRs256), Status::Ok);
  auto jwks = Jwks::createFrom(pubkey, Jwks::Type::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::Ok);
  fuzzJwtSignature(jwt, [&jwks](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwtVerificationFail);
  });
}

TEST(VerifyPKCSTestRs384, OKPem) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemRs384), Status::Ok);
  auto jwks = Jwks::createFrom(pubkey, Jwks::Type::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::Ok);
  fuzzJwtSignature(jwt, [&jwks](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwtVerificationFail);
  });
}

TEST(VerifyPKCSTestRs512, OKPem) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtPemRs512), Status::Ok);
  auto jwks = Jwks::createFrom(pubkey, Jwks::Type::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::Ok);
  fuzzJwtSignature(jwt, [&jwks](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks, 1), Status::JwtVerificationFail);
  });
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy
