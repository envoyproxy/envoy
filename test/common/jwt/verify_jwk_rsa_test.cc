// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/verify.h"

#include "test/common/jwt/test_common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

// SPELLCHECKER(off)
/*
  private key for kid=62a93512c9ee4c7f8067b5a216dade2763d32a47:

-----BEGIN RSA PRIVATE KEY-----
MIIEowIBAAKCAQEAtw7MNxUTxmzWROCD5BqJxmzT7xqc9KsnAjbXCoqEEHDx4WBl
fcwkXHt9e/2+Uwi3Arz3FOMNKwGGlbr7clBY3utsjUs8BTF0kO/poAmSTdSuGeh2
mSbcVHvmQ7X/kichWwx5Qj0Xj4REU3Gixu1gQIr3GATPAIULo5lj/ebOGAa+l0wI
G80Nzz1pBtTIUx68xs5ZGe7cIJ7E8n4pMX10eeuh36h+aossePeuHulYmjr4N0/1
jG7a+hHYL6nqwOR3ej0VqCTLS0OloC0LuCpLV7CnSpwbp2Qg/c+MDzQ0TH8g8drI
zR5hFe9a3NlNRMXgUU5RqbLnR9zfXr7b9oEszQIDAQABAoIBAQCgQQ8cRZJrSkqG
P7qWzXjBwfIDR1wSgWcD9DhrXPniXs4RzM7swvMuF1myW1/r1xxIBF+V5HNZq9tD
Z07LM3WpqZX9V9iyfyoZ3D29QcPX6RGFUtHIn5GRUGoz6rdTHnh/+bqJ92uR02vx
VPD4j0SNHFrWpxcE0HRxA07bLtxLgNbzXRNmzAB1eKMcrTu/W9Q1zI1opbsQbHbA
CjbPEdt8INi9ij7d+XRO6xsnM20KgeuKx1lFebYN9TKGEEx8BCGINOEyWx1lLhsm
V6S0XGVwWYdo2ulMWO9M0lNYPzX3AnluDVb3e1Yq2aZ1r7t/GrnGDILA1N2KrAEb
AAKHmYNNAoGBAPAv9qJqf4CP3tVDdto9273DA4Mp4Kjd6lio5CaF8jd/4552T3UK
N0Q7N6xaWbRYi6xsCZymC4/6DhmLG/vzZOOhHkTsvLshP81IYpWwjm4rF6BfCSl7
ip+1z8qonrElxes68+vc1mNhor6GGsxyGe0C18+KzpQ0fEB5J4p0OHGnAoGBAMMb
/fpr6FxXcjUgZzRlxHx1HriN6r8Jkzc+wAcQXWyPUOD8OFLcRuvikQ16sa+SlN4E
HfhbFn17ABsikUAIVh0pPkHqMsrGFxDn9JrORXUpNhLdBHa6ZH+we8yUe4G0X4Mc
R7c8OT26p2zMg5uqz7bQ1nJ/YWlP4nLqIytehnRrAoGAT6Rn0JUlsBiEmAylxVoL
mhGnAYAKWZQ0F6/w7wEtPs/uRuYOFM4NY1eLb2AKLK3LqqGsUkAQx23v7PJelh2v
z3bmVY52SkqNIGGnJuGDaO5rCCdbH2EypyCfRSDCdhUDWquSpBv3Dr8aOri2/CG9
jQSLUOtC8ouww6Qow1UkPjMCgYB8kTicU5ysqCAAj0mVCIxkMZqFlgYUJhbZpLSR
Tf93uiCXJDEJph2ZqLOXeYhMYjetb896qx02y/sLWAyIZ0ojoBthlhcLo2FCp/Vh
iOSLot4lOPsKmoJji9fei8Y2z2RTnxCiik65fJw8OG6mSm4HeFoSDAWzaQ9Y8ue1
XspVNQKBgAiHh4QfiFbgyFOlKdfcq7Scq98MA3mlmFeTx4Epe0A9xxhjbLrn362+
ZSCUhkdYkVkly4QVYHJ6Idzk47uUfEC6WlLEAnjKf9LD8vMmZ14yWR2CingYTIY1
LL2jMkSYEJx102t2088meCuJzEsF3BzEWOP8RfbFlciT7FFVeiM4
-----END RSA PRIVATE KEY-----
*/
// SPELLCHECKER(on)

// The following public key jwk and token are taken from
// https://github.com/cloudendpoints/esp/blob/master/src/api_manager/auth/lib/auth_jwt_validator_test.cc
const std::string PublicKeyRSA = R"(
{
  "keys": [
    {
      "kty": "RSA",
      "alg": "RS256",
      "use": "sig",
      "kid": "62a93512c9ee4c7f8067b5a216dade2763d32a47",
      "n": "0YWnm_eplO9BFtXszMRQNL5UtZ8HJdTH2jK7vjs4XdLkPW7YBkkm_2xNgcaVpkW0VT2l4mU3KftR-6s3Oa5Rnz5BrWEUkCTVVolR7VYksfqIB2I_x5yZHdOiomMTcm3DheUUCgbJRv5OKRnNqszA4xHn3tA3Ry8VO3X7BgKZYAUh9fyZTFLlkeAh0-bLK5zvqCmKW5QgDIXSxUTJxPjZCgfx1vmAfGqaJb-nvmrORXQ6L284c73DUL7mnt6wj3H6tVqPKA27j56N0TB1Hfx4ja6Slr8S4EB3F1luYhATa1PKUSH8mYDW11HolzZmTQpRoLV8ZoHbHEaTfqX_aYahIw",
      "e": "AQAB"
    },
    {
      "kty": "RSA",
      "alg": "RS256",
      "use": "sig",
      "kid": "b3319a147514df7ee5e4bcdee51350cc890cc89e",
      "n": "qDi7Tx4DhNvPQsl1ofxxc2ePQFcs-L0mXYo6TGS64CY_2WmOtvYlcLNZjhuddZVV2X88m0MfwaSA16wE-RiKM9hqo5EY8BPXj57CMiYAyiHuQPp1yayjMgoE1P2jvp4eqF-BTillGJt5W5RuXti9uqfMtCQdagB8EC3MNRuU_KdeLgBy3lS3oo4LOYd-74kRBVZbk2wnmmb7IhP9OoLc1-7-9qU1uhpDxmE6JwBau0mDSwMnYDS4G_ML17dC-ZDtLd1i24STUw39KH0pcSdfFbL2NtEZdNeam1DDdk0iUtJSPZliUHJBI_pj8M-2Mn_oA8jBuI8YKwBqYkZCN1I95Q",
      "e": "AQAB"
    }
  ]
}
)";

// SPELLCHECKER(off)
/*
  private key for kid=b3319a147514df7ee5e4bcdee51350cc890cc89e

-----BEGIN PRIVATE KEY-----
MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQCoOLtPHgOE289C
yXWh/HFzZ49AVyz4vSZdijpMZLrgJj/ZaY629iVws1mOG511lVXZfzybQx/BpIDX
rAT5GIoz2GqjkRjwE9ePnsIyJgDKIe5A+nXJrKMyCgTU/aO+nh6oX4FOKWUYm3lb
lG5e2L26p8y0JB1qAHwQLcw1G5T8p14uAHLeVLeijgs5h37viREFVluTbCeaZvsi
E/06gtzX7v72pTW6GkPGYTonAFq7SYNLAydgNLgb8wvXt0L5kO0t3WLbhJNTDf0o
fSlxJ18VsvY20Rl015qbUMN2TSJS0lI9mWJQckEj+mPwz7Yyf+gDyMG4jxgrAGpi
RkI3Uj3lAgMBAAECggEAOuaaVyp4KvXYDVeC07QTeUgCdZHQkkuQemIi5YrDkCZ0
Zsi6CsAG/f4eVk6/BGPEioItk2OeY+wYnOuDVkDMazjUpe7xH2ajLIt3DZ4W2q+k
v6WyxmmnPqcZaAZjZiPxMh02pkqCNmqBxJolRxp23DtSxqR6lBoVVojinpnIwem6
xyUl65u0mvlluMLCbKeGW/K9bGxT+qd3qWtYFLo5C3qQscXH4L0m96AjGgHUYW6M
Ffs94ETNfHjqICbyvXOklabSVYenXVRL24TOKIHWkywhi1wW+Q6zHDADSdDVYw5l
DaXz7nMzJ2X7cuRP9zrPpxByCYUZeJDqej0Pi7h7ZQKBgQDdI7Yb3xFXpbuPd1VS
tNMltMKzEp5uQ7FXyDNI6C8+9TrjNMduTQ3REGqEcfdWA79FTJq95IM7RjXX9Aae
p6cLekyH8MDH/SI744vCedkD2bjpA6MNQrzNkaubzGJgzNiZhjIAqnDAD3ljHI61
NbADc32SQMejb6zlEh8hssSsXwKBgQDCvXhTIO/EuE/y5Kyb/4RGMtVaQ2cpPCoB
GPASbEAHcsRk+4E7RtaoDQC1cBRy+zmiHUA9iI9XZyqD2xwwM89fzqMj5Yhgukvo
XMxvMh8NrTneK9q3/M3mV1AVg71FJQ2oBr8KOXSEbnF25V6/ara2+EpH2C2GDMAo
pgEnZ0/8OwKBgFB58IoQEdWdwLYjLW/d0oGEWN6mRfXGuMFDYDaGGLuGrxmEWZdw
fzi4CquMdgBdeLwVdrLoeEGX+XxPmCEgzg/FQBiwqtec7VpyIqhxg2J9V2elJS9s
PB1rh9I4/QxRP/oO9h9753BdsUU6XUzg7t8ypl4VKRH3UCpFAANZdW1tAoGAK4ad
tjbOYHGxrOBflB5wOiByf1JBZH4GBWjFf9iiFwgXzVpJcC5NHBKL7gG3EFwGba2M
BjTXlPmCDyaSDlQGLavJ2uQar0P0Y2MabmANgMkO/hFfOXBPtQQe6jAfxayaeMvJ
N0fQOylUQvbRTodTf2HPeG9g/W0sJem0qFH3FrECgYEAnwixjpd1Zm/diJuP0+Lb
YUzDP+Afy78IP3mXlbaQ/RVd7fJzMx6HOc8s4rQo1m0Y84Ztot0vwm9+S54mxVSo
6tvh9q0D7VLDgf+2NpnrDW7eMB3n0SrLJ83Mjc5rZ+wv7m033EPaWSr/TFtc/MaF
aOI20MEe3be96HHuWD3lTK0=
-----END PRIVATE KEY-----
*/

// JWT without kid
// Header:  {"alg":"RS256","typ":"JWT"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
// SPELLCHECKER(on)
const std::string JwtTextNoKid =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0.XYPg6VPrq-H1Kl-kgmAfGFomVpnmdZLIAo0g6dhJb2Be_"
    "koZ2T76xg5_Lr828hsLKxUfzwNxl5-k1cdz_kAst6vei0hdnOYqRQ8EhkZS_"
    "5Y2vWMrzGHw7AUPKCQvSnNqJG5HV8YdeOfpsLhQTd-"
    "tG61q39FWzJ5Ra5lkxWhcrVDQFtVy7KQrbm2dxhNEHAR2v6xXP21p1T5xFBdmGZbHFiH63N9"
    "dwdRgWjkvPVTUqxrZil7PSM2zg_GTBETp_"
    "qS7Wwf8C0V9o2KZu0KDV0j0c9nZPWTv3IMlaGZAtQgJUeyemzRDtf4g2yG3xBZrLm3AzDUj_"
    "EX_pmQAHA5ZjPVCAw";

// SPELLCHECKER(off)
// JWT without kid with long exp
// Header:  {"alg":"RS256","typ":"JWT"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","aud":"example_service","exp":2001001001}
// SPELLCHECKER(on)
const std::string JwtTextNoKidLongExp =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImF1ZCI6ImV4YW1wbGVfc2VydmljZSIsImV4cCI6MjAwMTAwMTAwMX0."
    "n45uWZfIBZwCIPiL0K8Ca3tmm-ZlsDrC79_"
    "vXCspPwk5oxdSn983tuC9GfVWKXWUMHe11DsB02b19Ow-"
    "fmoEzooTFn65Ml7G34nW07amyM6lETiMhNzyiunctplOr6xKKJHmzTUhfTirvDeG-q9n24-"
    "8lH7GP8GgHvDlgSM9OY7TGp81bRcnZBmxim_UzHoYO3_"
    "c8OP4ZX3xG5PfihVk5G0g6wcHrO70w0_64JgkKRCrLHMJSrhIgp9NHel_"
    "CNOnL0AjQKe9IGblJrMuouqYYS0zEWwmOVUWUSxQkoLpldQUVefcfjQeGjz8IlvktRa77FYe"
    "xfP590ACPyXrivtsxg";

// SPELLCHECKER(off)
// JWT with correct kid
// Header:
// {"alg":"RS256","typ":"JWT","kid":"b3319a147514df7ee5e4bcdee51350cc890cc89e"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
// SPELLCHECKER(on)
const std::string JwtTextWithCorrectKid =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImIzMzE5YTE0NzUxNGRmN2VlNWU0"
    "YmNkZWU1MTM1MGNjODkwY2M4OWUifQ."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0.QYWtQR2JNhLBJXtpJfFisF0WSyzLbD-9dynqwZt_"
    "KlQZAIoZpr65BRNEyRzpt0jYrk7RA7hUR2cS9kB3AIKuWA8kVZubrVhSv_fiX6phjf_"
    "bZYj92kDtMiPJf7RCuGyMgKXwwf4b1Sr67zamcTmQXf26DT415rnrUHVqTlOIW50TjNa1bbO"
    "fNyKZC3LFnKGEzkfaIeXYdGiSERVOTtOFF5cUtZA2OVyeAT3mE1NuBWxz0v7xJ4zdIwHwxFU"
    "wd_5tB57j_"
    "zCEC9NwnwTiZ8wcaSyMWc4GJUn4bJs22BTNlRt5ElWl6RuBohxZA7nXwWig5CoLZmCpYpb8L"
    "fBxyCpqJQ";

// SPELLCHECKER(off)
// JWT with correct kid and long claims
// Header:
// {"alg":"RS256","typ":"JWT","kid":"b3319a147514df7ee5e4bcdee51350cc890cc89e"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058,"scope":"xyzxyzxyz...(8000
// characters)"}
// SPELLCHECKER(on)
const std::string JwtTextWithLongClaimsCorrectKid =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImIzMzE5YTE0NzUxNGRmN2VlNWU0YmNkZWU1MTM1MGNjODkwY2"
    "M4OWUifQ."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MTUwMTI4MTA1OC"
    "wic2NvcGUiOiJ4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eX"
    "p4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4eXp4"
    "eXoifQ.TXqsys_"
    "wJgy8Knnsj0r5bw5189VeK8LZhFZofFiD3InHO4CqhKoZEIs2Jlksa7CmB0DSZXBE96EhoF5XZQGxZP5QdztGOhmOIgMXq"
    "Jb1MD1jvIoC6tPn7rdrz9LocHJ0b5vCosJ2wSU3ygDY-sZlM_"
    "YLNGeIvC3sBtB8INOXvlUpvnTewKihDfgA0chINhPUHGg_xaYlepJs9e11Z59GYuRC-"
    "DHAEeeYaqoZ1iMoYMAiIfVOAUaCxiQNZXFyhyht3gjYP4v4_"
    "IcEtYMyxGeNm87K1XmyUoHj4wW1A5aVRDMVBTskG8KtsyI110aj0wyBjLqfLKBqsQskcQTnDkk4UQ";

// SPELLCHECKER(off)
// JWT with existing but incorrect kid
// Header:
// {"alg":"RS256","typ":"JWT","kid":"62a93512c9ee4c7f8067b5a216dade2763d32a47"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
// SPELLCHECKER(on)
const std::string JwtTextWithIncorrectKid =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6IjYyYTkzNTEyYzllZTRjN2Y4MDY3"
    "YjVhMjE2ZGFkZTI3NjNkMzJhNDcifQ."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0."
    "adrKqsjKh4zdOuw9rMZr0Kn2LLYG1OUfDuvnO6tk75NKCHpKX6oI8moNYhgcCQU4AoCKXZ_"
    "u-oMl54QTx9lX9xZ2VUWKTxcJEOnpoJb-DVv_FgIG9ETe5wcCS8Y9pQ2-hxtO1_LWYok1-"
    "A01Q4929u6WNw_Og4rFXR6VSpZxXHOQrEwW44D2-Lngu1PtPjWIz3rO6cOiYaTGCS6-"
    "TVeLFnB32KQg823WhFhWzzHjhYRO7NOrl-IjfGn3zYD_"
    "DfSoMY3A6LeOFCPp0JX1gcKcs2mxaF6e3LfVoBiOBZGvgG_"
    "jx3y85hF2BZiANbSf1nlLQFdjk_CWbLPhTWeSfLXMOg";

// SPELLCHECKER(off)
// JWT with nonexist kid
// Header:  {"alg":"RS256","typ":"JWT","kid":"blahblahblah"}
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":1501281058}
// SPELLCHECKER(on)
const std::string JwtTextWithNonExistKid =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImJsYWhibGFoYmxhaCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIs"
    "ImV4cCI6MTUwMTI4MTA1OH0.digk0Fr_IdcWgJNVyeVDw2dC1cQG6LsHwg5pIN93L4_"
    "xhEDI3ZFoZ8aE44kvQHWLicnHDlhELqtF-"
    "TqxrhfnitpLE7jiyknSu6NVXxtRBcZ3dOTKryVJDvDXcYXOaaP8infnh82loHfhikgg1xmk9"
    "rcH50jtc3BkxWNbpNgPyaAAE2tEisIInaxeX0gqkwiNVrLGe1hfwdtdlWFL1WENGlyniQBvB"
    "Mwi8DgG_F0eyFKTSRWoaNQQXQruEK0YIcwDj9tkYOXq8cLAnRK9zSYc5-"
    "15Hlzfb8eE77pID0HZN-Axeui4IY22I_kYftd0OEqlwXJv_v5p6kNaHsQ9QbtAkw";

class VerifyJwkRsaTest : public testing::Test {
protected:
  void SetUp() {
    jwks_ = Jwks::createFrom(PublicKeyRSA, Jwks::Type::JWKS);
    EXPECT_EQ(jwks_->getStatus(), Status::Ok);
  }

  JwksPtr jwks_;
};

TEST_F(VerifyJwkRsaTest, NoKidOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextNoKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);

  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

TEST_F(VerifyJwkRsaTest, NoKidLongExpOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextNoKidLongExp), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_), Status::Ok);

  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

TEST_F(VerifyJwkRsaTest, CorrectKidOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextWithCorrectKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);

  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

TEST_F(VerifyJwkRsaTest, NonExistKidFail) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextWithNonExistKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwksKidAlgMismatch);
}

TEST_F(VerifyJwkRsaTest, OkPublicKeyNotAlg) {
  // Remove "alg" claim from public key.
  std::string alg_claim = R"("alg": "RS256",)";
  std::string pubkey_no_alg = PublicKeyRSA;
  std::size_t alg_pos = pubkey_no_alg.find(alg_claim);
  while (alg_pos != std::string::npos) {
    pubkey_no_alg.erase(alg_pos, alg_claim.length());
    alg_pos = pubkey_no_alg.find(alg_claim);
  }

  jwks_ = Jwks::createFrom(pubkey_no_alg, Jwks::Type::JWKS);
  EXPECT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextNoKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);
}

TEST_F(VerifyJwkRsaTest, OkPublicKeyNotKid) {
  // Remove "kid" claim from public key.
  std::string kid_claim1 = R"("kid": "62a93512c9ee4c7f8067b5a216dade2763d32a47",)";
  std::string kid_claim2 = R"("kid": "b3319a147514df7ee5e4bcdee51350cc890cc89e",)";
  std::string pubkey_no_kid = PublicKeyRSA;
  std::size_t kid_pos = pubkey_no_kid.find(kid_claim1);
  pubkey_no_kid.erase(kid_pos, kid_claim1.length());
  kid_pos = pubkey_no_kid.find(kid_claim2);
  pubkey_no_kid.erase(kid_pos, kid_claim2.length());

  jwks_ = Jwks::createFrom(pubkey_no_kid, Jwks::Type::JWKS);
  EXPECT_EQ(jwks_->getStatus(), Status::Ok);

  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextNoKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);
}

TEST_F(VerifyJwkRsaTest, LongClaimsWithCorrectKidOk) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextWithLongClaimsCorrectKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);

  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy
