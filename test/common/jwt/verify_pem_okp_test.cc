// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef BORINGSSL_FIPS

#include "gtest/gtest.h"
#include "source/common/jwt/verify.h"
#include "test/test_common.h"

namespace Envoy {
namespace JwtVerify {
namespace {

// To generate new keys:
// $ openssl genpkey -algorithm ed25519 -out ed_private.pem
// $ openssl pkey -in ed_private.pem -pubout -out ed_public.pem
// To generate new JWTs: I used https://pypi.org/project/privex-pyjwt/

// ED25519 private key:
// "-----BEGIN PRIVATE KEY-----"
// "MC4CAQAwBQYDK2VwBCIEIHU2mIWEGpLJ4f6wz0+6DZOCpQ3c/HrqQP5i3LDi6BLe"
// "-----END PRIVATE KEY-----"

const std::string ed25519pubkey = R"(
-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEA6hH43mEbo+h7iigPm9zLKHH5oEc+bjIXD/t4PLPqHLQ=
-----END PUBLIC KEY-----
)";

// JWT with
// Header:  {"alg": "EdDSA", "typ": "JWT"}
// Payload: {"iss":"https://example.com", "sub":"test@example.com"}
const std::string JwtPemEd25519 =
    "eyJ0eXAiOiJKV1QiLCJhbGciOiJFZERTQSJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSJ9."
    "rn-h5xTejtilHiAG6aKJEQ3e5_"
    "aIKC7nwKUPOjBqN8df69JLiFtKxFCDINHtCNhoeLkgcDHHo2SJFincVH_OCg";

// Header:  {"alg": "EdDSA", typ": "JWT"}
// Payload: {"iss":"https://example.com", "sub":"test@example.com"}
// But signed by a different key
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

}  // namespace
}  // namespace JwtVerify
}  // namespace Envoy

#endif
