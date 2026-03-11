// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/verify.h"

#include "test/common/jwt/test_common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

// JWT with
// Header:  {"alg":"RS256","typ":"JWT"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","aud":["aud1"]}
const std::string JwtOneAudtext =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsIm"
    "F1ZCI6WyJhdWQxIl19Cg."
    "OldJUf2bMFr09QhqeJ3R6NJQSn22nFTcrsTCc7kVDsGWPPGe_-"
    "C7AyiPWQEzfc2wrwIVDsCrANtetlNOwBlUjQwQyCP90ByUlP9bt5TawHpl8iHXo3qs-"
    "WCJNT3yNlUnLa8sIBQWjVCE5NrWDirH755bESHzu25CEGwkISYyY5RU_wTA4_YoX-_"
    "TGgTs84fmCmgnpsqRLESXBAcOzO3Fo8y8Pz6IlHOWJAnxm2uRJm9Yko_6-"
    "Xz8OdDrJhH6iun44I_AC0qjj2bhyLRO4bli12U2Z5MRw9sXjCy55q43rYLAFt_"
    "hXFVY7vK9vU9a38gsFEGghv4oYCPWECp-xR5cHA";
// SPELLCHECKER(off)
/*
"-----BEGIN RSA PRIVATE KEY-----"
"MIIEowIBAAKCAQEAtw7MNxUTxmzWROCD5BqJxmzT7xqc9KsnAjbXCoqEEHDx4WBl"
"fcwkXHt9e/2+Uwi3Arz3FOMNKwGGlbr7clBY3utsjUs8BTF0kO/poAmSTdSuGeh2"
"mSbcVHvmQ7X/kichWwx5Qj0Xj4REU3Gixu1gQIr3GATPAIULo5lj/ebOGAa+l0wI"
"G80Nzz1pBtTIUx68xs5ZGe7cIJ7E8n4pMX10eeuh36h+aossePeuHulYmjr4N0/1"
"jG7a+hHYL6nqwOR3ej0VqCTLS0OloC0LuCpLV7CnSpwbp2Qg/c+MDzQ0TH8g8drI"
"zR5hFe9a3NlNRMXgUU5RqbLnR9zfXr7b9oEszQIDAQABAoIBAQCgQQ8cRZJrSkqG"
"P7qWzXjBwfIDR1wSgWcD9DhrXPniXs4RzM7swvMuF1myW1/r1xxIBF+V5HNZq9tD"
"Z07LM3WpqZX9V9iyfyoZ3D29QcPX6RGFUtHIn5GRUGoz6rdTHnh/+bqJ92uR02vx"
"VPD4j0SNHFrWpxcE0HRxA07bLtxLgNbzXRNmzAB1eKMcrTu/W9Q1zI1opbsQbHbA"
"CjbPEdt8INi9ij7d+XRO6xsnM20KgeuKx1lFebYN9TKGEEx8BCGINOEyWx1lLhsm"
"V6S0XGVwWYdo2ulMWO9M0lNYPzX3AnluDVb3e1Yq2aZ1r7t/GrnGDILA1N2KrAEb"
"AAKHmYNNAoGBAPAv9qJqf4CP3tVDdto9273DA4Mp4Kjd6lio5CaF8jd/4552T3UK"
"N0Q7N6xaWbRYi6xsCZymC4/6DhmLG/vzZOOhHkTsvLshP81IYpWwjm4rF6BfCSl7"
"ip+1z8qonrElxes68+vc1mNhor6GGsxyGe0C18+KzpQ0fEB5J4p0OHGnAoGBAMMb"
"/fpr6FxXcjUgZzRlxHx1HriN6r8Jkzc+wAcQXWyPUOD8OFLcRuvikQ16sa+SlN4E"
"HfhbFn17ABsikUAIVh0pPkHqMsrGFxDn9JrORXUpNhLdBHa6ZH+we8yUe4G0X4Mc"
"R7c8OT26p2zMg5uqz7bQ1nJ/YWlP4nLqIytehnRrAoGAT6Rn0JUlsBiEmAylxVoL"
"mhGnAYAKWZQ0F6/w7wEtPs/uRuYOFM4NY1eLb2AKLK3LqqGsUkAQx23v7PJelh2v"
"z3bmVY52SkqNIGGnJuGDaO5rCCdbH2EypyCfRSDCdhUDWquSpBv3Dr8aOri2/CG9"
"jQSLUOtC8ouww6Qow1UkPjMCgYB8kTicU5ysqCAAj0mVCIxkMZqFlgYUJhbZpLSR"
"Tf93uiCXJDEJph2ZqLOXeYhMYjetb896qx02y/sLWAyIZ0ojoBthlhcLo2FCp/Vh"
"iOSLot4lOPsKmoJji9fei8Y2z2RTnxCiik65fJw8OG6mSm4HeFoSDAWzaQ9Y8ue1"
"XspVNQKBgAiHh4QfiFbgyFOlKdfcq7Scq98MA3mlmFeTx4Epe0A9xxhjbLrn362+"
"ZSCUhkdYkVkly4QVYHJ6Idzk47uUfEC6WlLEAnjKf9LD8vMmZ14yWR2CingYTIY1"
"LL2jMkSYEJx102t2088meCuJzEsF3BzEWOP8RfbFlciT7FFVeiM4"
"-----END RSA PRIVATE KEY-----"
 */
// SPELLCHECKER(on)
const std::string PublicKeyRSA = R"(
{
  "keys":[
  {
    "kty":"RSA",
    "e":"AQAB",
    "n":"tw7MNxUTxmzWROCD5BqJxmzT7xqc9KsnAjbXCoqEEHDx4WBlfcwkXHt9e_2-Uwi3Arz3FOMNKwGGlbr7clBY3utsjUs8BTF0kO_poAmSTdSuGeh2mSbcVHvmQ7X_kichWwx5Qj0Xj4REU3Gixu1gQIr3GATPAIULo5lj_ebOGAa-l0wIG80Nzz1pBtTIUx68xs5ZGe7cIJ7E8n4pMX10eeuh36h-aossePeuHulYmjr4N0_1jG7a-hHYL6nqwOR3ej0VqCTLS0OloC0LuCpLV7CnSpwbp2Qg_c-MDzQ0TH8g8drIzR5hFe9a3NlNRMXgUU5RqbLnR9zfXr7b9oEszQ"
  }]
}
)";

TEST(VerifyAudTest, MissingAudience) {
  Jwt jwt;
  Jwks jwks;
  std::vector<std::string> audiences = {"aud2", "aud3"};
  EXPECT_EQ(jwt.parseFromString(JwtOneAudtext), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, jwks, audiences), Status::JwtAudienceNotAllowed);
}

TEST(VerifyAudTest, Success) {
  Jwt jwt;
  auto jwks = Jwks::createFrom(PublicKeyRSA, Jwks::Type::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwt.parseFromString(JwtOneAudtext), Status::Ok);

  // Verify when at least one audience appears in both the JWT
  // and the allowed list.
  std::vector<std::string> audiences = {"aud1", "aud3"};
  EXPECT_EQ(verifyJwt(jwt, *jwks, audiences), Status::Ok);
  // Verify that when the allowed list is empty, verification succeeds.
  EXPECT_EQ(verifyJwt(jwt, *jwks, std::vector<std::string>{}), Status::Ok);
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy
