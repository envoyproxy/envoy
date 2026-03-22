// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/jwks.h"

#include "test/common/jwt/test_common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

TEST(JwksParseTest, GoodJwks) {
  const std::string jwks_text = R"(
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

  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 2);

  EXPECT_EQ(jwks->keys()[0]->alg_, "RS256");
  EXPECT_EQ(jwks->keys()[0]->kid_, "62a93512c9ee4c7f8067b5a216dade2763d32a47");

  EXPECT_EQ(jwks->keys()[1]->alg_, "RS256");
  EXPECT_EQ(jwks->keys()[1]->kid_, "b3319a147514df7ee5e4bcdee51350cc890cc89e");
}

TEST(JwksParseTest, GoodEC) {
  // Public key JwkEC
  const std::string jwks_text = R"(
    {
       "keys": [
          {
             "kty": "EC",
             "crv": "P-256",
             "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
             "y": "92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8",
             "alg": "ES256",
             "kid": "abc"
          },
          {
             "kty": "EC",
             "crv": "P-256",
             "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
             "y": "92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8",
             "alg": "ES256",
             "kid": "xyz"
          },
          {
             "kty": "EC",
             "crv": "P-384",
             "x": "yY8DWcyWlrr93FTrscI5Ydz2NC7emfoKYHJLX2dr3cSgfw0GuxAkuQ5nBMJmVV5g",
             "y": "An5wVxEfksDOa_zvSHHGkeYJUfl8y11wYkOlFjBt9pOCw5-RlfZgPOa3pbmUquxZ",
             "alg": "ES384",
             "kid": "es384"
          },
          {
             "kty": "EC",
             "crv": "P-521",
             "x": "Abijiex7rz7t-_Zj_E6Oo0OXe9C_-MCSD-OWio15ATQGjH9WpbWjN62ZqrrU_nwJiqqwx6ZsYKhUc_J3PRaMbdVC",
             "y": "FxaljCIuoVEA7PJIaDPJ5ePXtZ0hkinT1B_bQ91mShCiR_43Whsn1P7Gz30WEnLuJs1SGVz1oT4lIRUYni2OfIk",
             "alg": "ES512",
             "kid": "es512"
          }
      ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 4);

  EXPECT_EQ(jwks->keys()[0]->alg_, "ES256");
  EXPECT_EQ(jwks->keys()[0]->kid_, "abc");
  EXPECT_EQ(jwks->keys()[0]->kty_, "EC");
  EXPECT_EQ(jwks->keys()[0]->crv_, "P-256");

  EXPECT_EQ(jwks->keys()[1]->alg_, "ES256");
  EXPECT_EQ(jwks->keys()[1]->kid_, "xyz");
  EXPECT_EQ(jwks->keys()[1]->kty_, "EC");
  EXPECT_EQ(jwks->keys()[1]->crv_, "P-256");

  EXPECT_EQ(jwks->keys()[2]->alg_, "ES384");
  EXPECT_EQ(jwks->keys()[2]->kid_, "es384");
  EXPECT_EQ(jwks->keys()[2]->kty_, "EC");
  EXPECT_EQ(jwks->keys()[2]->crv_, "P-384");

  EXPECT_EQ(jwks->keys()[3]->alg_, "ES512");
  EXPECT_EQ(jwks->keys()[3]->kid_, "es512");
  EXPECT_EQ(jwks->keys()[3]->kty_, "EC");
  EXPECT_EQ(jwks->keys()[3]->crv_, "P-521");
}

TEST(JwksParseTest, GoodOKP) {
  const std::string jwks_text = R"(
    {
      "keys": [
        {
          "kty": "OKP",
          "crv": "Ed25519",
          "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
          "alg": "EdDSA",
          "kid": "ed25519"
        }
      ]
    }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 1);

  EXPECT_EQ(jwks->keys()[0]->alg_, "EdDSA");
  EXPECT_EQ(jwks->keys()[0]->kid_, "ed25519");
  EXPECT_EQ(jwks->keys()[0]->kty_, "OKP");
  EXPECT_EQ(jwks->keys()[0]->crv_, "Ed25519");
}

TEST(JwksParseTest, EmptyJwks) {
  auto jwks = Jwks::createFrom("", Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksParseError);
}

TEST(JwksParseTest, JwksNoKeys) {
  auto jwks = Jwks::createFrom("{}", Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksNoKeys);
}

TEST(JwksParseTest, JwksWrongKeys) {
  auto jwks = Jwks::createFrom(R"({"keys": 123})", Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksBadKeys);
}

TEST(JwksParseTest, JwksInvalidKty) {
  // Invalid kty field
  const std::string jwks_text = R"(
   {
      "keys": [
        {
           "kty": "XYZ",
           "crv": "P-256",
           "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
           "y": "92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8",
           "alg": "ES256",
           "kid": "abc"
        }
     ]
   }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksNotImplementedKty);
}

TEST(JwksParseTest, JwksMismatchKty1) {
  // kty doesn't match with alg
  const std::string jwks_text = R"(
     {
        "keys": [
           {
              "kty": "RSA",
              "alg": "ES256"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksRSAKeyBadAlg);
}

TEST(JwksParseTest, JwksMismatchKty2) {
  // kty doesn't match with alg
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "RS256"
           }
        ]
     }
)";

  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksECKeyBadAlg);
}

TEST(JwksParseTest, JwksIgnoreUnsupportedKey) {
  // An unsupported key is ignored and no error is returned, even though the
  // unsupported key is the last key.
  const std::string jwks_text = R"(
    {
      "keys": [
        {
          "kty": "OKP",
          "crv": "Ed25519",
          "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
          "alg": "EdDSA",
          "kid": "one"
        },
        {
          "kty": "OKP",
          "crv": "X25519",
          "x": "GiUzxNZKFGhuciavqsugxhvs40PGQ7C4tQzKBUGttEE",
          "alg": "EdDSA",
          "kid": "two"
        }
      ]
    }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 1);

  EXPECT_EQ(jwks->keys()[0]->alg_, "EdDSA");
  EXPECT_EQ(jwks->keys()[0]->kid_, "one");
  EXPECT_EQ(jwks->keys()[0]->kty_, "OKP");
  EXPECT_EQ(jwks->keys()[0]->crv_, "Ed25519");
}

TEST(JwksParseTest, JwksIgnoreUnsupportedKeyInMiddle) {
  // An unsupported key is ignored even though it is in the middle of the
  // JWKS, good keys after the unsupported key are still read.
  const std::string jwks_text = R"(
    {
      "keys": [
        {
          "kty": "OKP",
          "crv": "Ed25519",
          "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
          "alg": "EdDSA",
          "kid": "one"
        },
        {
          "kty": "OKP",
          "crv": "X25519",
          "x": "GiUzxNZKFGhuciavqsugxhvs40PGQ7C4tQzKBUGttEE",
          "alg": "EdDSA",
          "kid": "two"
        },
        {
           "kty": "EC",
           "crv": "P-256",
           "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
           "y": "92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8",
           "alg": "ES256",
           "kid": "three"
        }
      ]
    }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 2);

  EXPECT_EQ(jwks->keys()[0]->alg_, "EdDSA");
  EXPECT_EQ(jwks->keys()[0]->kid_, "one");
  EXPECT_EQ(jwks->keys()[0]->kty_, "OKP");
  EXPECT_EQ(jwks->keys()[0]->crv_, "Ed25519");

  EXPECT_EQ(jwks->keys()[1]->alg_, "ES256");
  EXPECT_EQ(jwks->keys()[1]->kid_, "three");
  EXPECT_EQ(jwks->keys()[1]->kty_, "EC");
  EXPECT_EQ(jwks->keys()[1]->crv_, "P-256");
}

TEST(JwksParseTest, JwksIgnoreUnsupportedKeySolo) {
  // If the only key is unsupported then an error is returned.
  const std::string jwks_text = R"(
    {
      "keys": [
        {
          "kty": "OKP",
          "crv": "X25519",
          "x": "GiUzxNZKFGhuciavqsugxhvs40PGQ7C4tQzKBUGttEE",
          "alg": "EdDSA",
          "kid": "two"
        }
      ]
    }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksOKPKeyCrvUnsupported);
  EXPECT_EQ(jwks->keys().size(), 0);
}

TEST(JwksParseTest, JwksECNoXY) {
  const std::string jwks_text = R"(
     {
        "keys": [
           {
              "kty": "EC",
              "alg": "ES256"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksECKeyMissingX);
}

TEST(JwksParseTest, JwksRSANoNE) {
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "RSA",
               "alg": "RS256"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksRSAKeyMissingN);
}

TEST(JwksParseTest, JwksRSAKeyBadN) {
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "RSA",
               "alg": "RS256",
               "n":   1234
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksRSAKeyBadN);
}

TEST(JwksParseTest, JwksRSAKeyMissingE) {
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "RSA",
               "alg": "RS256",
               "n":   "NNNNN"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksRSAKeyMissingE);
}

TEST(JwksParseTest, JwksRSAKeyBadE) {
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "RSA",
               "alg": "RS256",
               "n":   "NNNNN",
               "e":   1234
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksRSAKeyBadE);
}

TEST(JwksParseTest, JwksECXYBadBase64) {
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "x": "~}}",
               "y": "92bCBTvMFQ8lKbS2MbgjT3Yf",
               "alg": "ES256"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksEcXorYBadBase64);
}

TEST(JwksParseTest, JwksECWrongXY) {
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k111",
               "y": "92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8111",
               "alg": "ES256"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksEcParseError);
}

TEST(JwksParseTest, JwksRSAWrongNE) {
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "RSA",
               "n": "EB54wykhS7YJFD6RYJNnwbW",
               "e": "92bCBTvMFQ8lKbS2MbgjT3YfmY",
               "alg": "RS256"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksRsaParseError);
}

TEST(JwksParseTest, JwksRSAInvalidN) {
  const std::string BadPublicKeyRSA =
      "{\n"
      " \"keys\": [\n"
      " {\n"
      " \"alg\": \"RS256\",\n"
      " \"kty\": \"RSA\",\n"
      " \"use\": \"sig\",\n"
      " \"x5c\": "
      "[\"MIIDjjCCAnYCCQDM2dGMrJDL3TANBgkqhkiG9w0BAQUFADCBiDEVMBMGA1UEAwwMd3d3L"
      "mRlbGwuY29tMQ0wCwYDVQQKDARkZWxsMQ0wCwYDVQQLDARkZWxsMRIwEAYDVQQHDAlCYW5nY"
      "WxvcmUxEjAQBgNVBAgMCUthcm5hdGFrYTELMAkGA1UEBhMCSU4xHDAaBgkqhkiG9w0BCQEWD"
      "WFiaGlAZGVsbC5jb20wHhcNMTkwNjI1MDcwNjM1WhcNMjAwNjI0MDcwNjM1WjCBiDEVMBMGA"
      "1UEAwwMd3d3LmRlbGwuY29tMQ0wCwYDVQQKDARkZWxsMQ0wCwYDVQQLDARkZWxsMRIwEAYDV"
      "QQHDAlCYW5nYWxvcmUxEjAQBgNVBAgMCUthcm5hdGFrYTELMAkGA1UEBhMCSU4xHDAaBgkqh"
      "kiG9w0BCQEWDWFiaGlAZGVsbC5jb20wggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBA"
      "QDlE7W15NCXoIZX+"
      "uE7HF0LTnfgBpaqoYyQFDmVUNEd0WWV9nX04c3iyxZSpoTsoUZktNd0CUyC8oVRg2xxdPxA2"
      "aRVpNMwsDkuDnOZPNZZCS64QmMD7V5ebSAi4vQ7LH6zo9DCVwjzW10ZOZ3WHAyoKuNVGeb5w"
      "2+xDQM1mFqApy6KB7M/b3KG7cqpZfPn9Ebd1Uyk+8WY/"
      "IxJvb7EHt06Z+8b3F+LkRp7UI4ykkVkl3XaiBlG56ZyHfvH6R5Jy+"
      "8P0vl4wtX86N6MS48TZPhGAoo2KwWsOEGxve005ZK6LkHwxMsOD98yvLM7AG0SBxVF8O8KeZ"
      "/nbTP1oVSq6aEFAgMBAAEwDQYJKoZIhvcNAQEFBQADggEBAGEhT6xuZqyZb/"
      "K6aI61RYy4tnR92d97H+zcL9t9/"
      "8FyH3qIAjIM9+qdr7dLLnVcNMmwiKzZpsBywno72z5gG4l6/TicBIJfI2BaG9JVdU3/"
      "wscPlqazwI/"
      "d1LvIkWSzrFQ2VdTPSYactPzGWddlx9QKU9cIKcNPcWdg0S0q1Khu8kejpJ+"
      "EUtSMc8OonFV99r1juFzVPtwGihuc6R7T/"
      "GnWgYLmhoCCaQKdLWn7FIyQH2WZ10CI6as+"
      "zKkylDkVnbsJYFabvbgRrNNl4RGXXm5D0lk9cwo1Srd28wEhi35b8zb1p0eTamS6qTpjHtc6"
      "DpgZK3MavFVdaFfR9bEYpHc=\"],\n"
      " \"n\": "
      "\"5RO1teTQl6CGV/"
      "rhOxxdC0534AaWqqGMkBQ5lVDRHdFllfZ19OHN4ssWUqaE7KFGZLTXdAlMgvKFUYNscXT8QN"
      "mkVaTTMLA5Lg5zmTzWWQkuuEJjA+1eXm0gIuL0Oyx+s6PQwlcI81tdGTmd1hwMqCrjVRnm+"
      "cNvsQ0DNZhagKcuigezP29yhu3KqWXz5/"
      "RG3dVMpPvFmPyMSb2+xB7dOmfvG9xfi5Eae1COMpJFZJd12ogZRuemch37x+"
      "keScvvD9L5eMLV/OjejEuPE2T4RgKKNisFrDhBsb3tNOWSui5B8MTLDg/"
      "fMryzOwBtEgcVRfDvCnmf520z9aFUqumhBQ\",\n"
      " \"e\": \"AQAB\",\n"
      " \"kid\": \"F46BB2F600BF3BBB53A324F12B290846\",\n"
      " \"x5t\": \"F46BB2F600BF3BBB53A324F12B290846\"\n"
      " }\n"
      " ]\n"
      "}";
  auto jwks = Jwks::createFrom(BadPublicKeyRSA, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksRsaParseError);
}

TEST(JwksParseTest, JwksOKPXBadBase64) {
  // OKP x is invalid base64
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "OKP",
               "crv": "Ed25519",
               "x": "~}}"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksOKPXBadBase64);
}

TEST(JwksParseTest, JwksOKPXWrongLength) {
  // OKP x is the wrong length
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "OKP",
               "crv": "Ed25519",
               "x": "dGVzdAo"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksOKPXWrongLength);
}

TEST(JwksParseTest, JwksECMatchAlgES256WrongCrvType) {
  // Wrong "crv" data type
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES256",
               "crv": 1234
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksECKeyBadCrv);
}

TEST(JwksParseTest, JwksECMatchAlgES256WrongXType) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES256",
               "crv": "P-256",
               "x":   1234
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksECKeyBadX);
}

TEST(JwksParseTest, JwksECMatchAlgES256WrongMissingY) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES256",
               "crv": "P-256",
               "x":   "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksECKeyMissingY);
}

TEST(JwksParseTest, JwksECMatchAlgES256WrongBadY) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES256",
               "crv": "P-256",
               "x":   "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
               "y":   1234
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksECKeyBadY);
}

TEST(JwksParseTest, JwksJwkMissingKty) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
              "alg": "ES256",
              "crv": "P-256",
              "x":   "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksMissingKty);
}

TEST(JwksParseTest, JwksJwkBadKty) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
              "kty": 1234,
              "alg": "ES256",
              "crv": "P-256",
              "x":   "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksBadKty);
}

TEST(JwksParseTest, JwksJwkOctBadAlg) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
              "kty": "oct",
              "alg": "HS333"
            }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksHMACKeyBadAlg);
}

TEST(JwksParseTest, JwksJwkOctBadMissingK) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
              "kty": "oct",
              "alg": "HS256",
              "use": "sig",
              "kid": "62a93512c9ee4c7f8067b5a216dade2763d32a47"
            }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksHMACKeyMissingK);
}

TEST(JwksParseTest, JwksJwkOctBadK) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
              "kty": "oct",
              "alg": "HS256",
              "use": "sig",
              "kid": "62a93512c9ee4c7f8067b5a216dade2763d32a47",
              "k":   12345
            }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksHMACKeyBadK);
}

TEST(JwksParseTest, JwksJwkOctBadBase64) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
              "kty": "oct",
              "alg": "HS256",
              "use": "sig",
              "kid": "62a93512c9ee4c7f8067b5a216dade2763d32a47",
              "k":   "12345"
            }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksOctBadBase64);
}

TEST(JwksParseTest, JwksECMatchAlgES256CrvP256) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES256",
               "crv": "P-256",
               "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
               "y": "92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
}

TEST(JwksParseTest, JwksECMatchAlgES384CrvP384) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES384",
               "crv": "P-384",
               "x": "yY8DWcyWlrr93FTrscI5Ydz2NC7emfoKYHJLX2dr3cSgfw0GuxAkuQ5nBMJmVV5g",
               "y": "An5wVxEfksDOa_zvSHHGkeYJUfl8y11wYkOlFjBt9pOCw5-RlfZgPOa3pbmUquxZ"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
}

TEST(JwksParseTest, JwksECMatchAlgES512CrvP521) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES512",
               "crv": "P-521",
               "x": "Abijiex7rz7t-_Zj_E6Oo0OXe9C_-MCSD-OWio15ATQGjH9WpbWjN62ZqrrU_nwJiqqwx6ZsYKhUc_J3PRaMbdVC",
               "y": "FxaljCIuoVEA7PJIaDPJ5ePXtZ0hkinT1B_bQ91mShCiR_43Whsn1P7Gz30WEnLuJs1SGVz1oT4lIRUYni2OfIk"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
}

TEST(JwksParseTest, JwksECMissingBothAlgCrvES256) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
               "y": "92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
}

TEST(JwksParseTest, JwksECMissingBothAlgES384) {
  // alg matches crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "x": "yY8DWcyWlrr93FTrscI5Ydz2NC7emfoKYHJLX2dr3cSgfw0GuxAkuQ5nBMJmVV5g",
               "y": "An5wVxEfksDOa_zvSHHGkeYJUfl8y11wYkOlFjBt9pOCw5-RlfZgPOa3pbmUquxZ"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  // It should fail since it is ES384, but we default to ES256
  EXPECT_EQ(jwks->getStatus(), Status::JwksEcParseError);
}

TEST(JwksParseTest, JwksECMismatchAlgCrv1) {
  // alg doesn't match with crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES256",
               "crv": "P-384"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksECKeyAlgNotCompatibleWithCrv);
}

TEST(JwksParseTest, JwkECMissingAlg) {
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "crv": "P-521",
               "kid": "sxG_WeuLxIKXoVit-8vyQf",
               "kty": "EC",
               "use": "sig",
               "x": "AG3w2vYgVbn4E27rkxZPUVrzLWhMctY5GOP6xygLLFwNRaoOx2gnlQPwAsEXHxz80u5lfmOms0pJSjuDrNqs5pB4",
               "y": "Ad0K-hbFmTVj3nMOw7jAdl21dlU35pG1g7h_Tswr0VYfxqg4ubIPyXrrtmlKH8q3c2Gqgq77Uq12qfcDE8zF2a4v"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
}

TEST(JwksParseTest, JwkECMissingCrv) {
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "alg": "ES512",
               "kid": "sxG_WeuLxIKXoVit-8vyQf",
               "kty": "EC",
               "use": "sig",
               "x": "AG3w2vYgVbn4E27rkxZPUVrzLWhMctY5GOP6xygLLFwNRaoOx2gnlQPwAsEXHxz80u5lfmOms0pJSjuDrNqs5pB4",
               "y": "Ad0K-hbFmTVj3nMOw7jAdl21dlU35pG1g7h_Tswr0VYfxqg4ubIPyXrrtmlKH8q3c2Gqgq77Uq12qfcDE8zF2a4v"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
}

TEST(JwksParseTest, JwksECMismatchAlgCrv2) {
  // alg doesn't match with crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES384",
               "crv": "P-521"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksECKeyAlgNotCompatibleWithCrv);
}

TEST(JwksParseTest, JwksECMismatchAlgCrv3) {
  // alg doesn't match with crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES512",
               "crv": "P-256"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksECKeyAlgNotCompatibleWithCrv);
}

TEST(JwksParseTest, JwksECNotSupportedAlg) {
  // alg doesn't match with crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES1024",
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksECKeyAlgOrCrvUnsupported);
}

TEST(JwksParseTest, JwksECNotSupportedCrv) {
  // alg doesn't match with crv
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "crv": "P-1024"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksECKeyAlgOrCrvUnsupported);
}

TEST(JwksParseTest, JwksECUnspecifiedCrv) {
  // crv determined from alg
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "EC",
               "alg": "ES256",
               "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
               "y": "92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8"
           },
           {
               "kty": "EC",
               "alg": "ES384",
               "x": "yY8DWcyWlrr93FTrscI5Ydz2NC7emfoKYHJLX2dr3cSgfw0GuxAkuQ5nBMJmVV5g",
               "y": "An5wVxEfksDOa_zvSHHGkeYJUfl8y11wYkOlFjBt9pOCw5-RlfZgPOa3pbmUquxZ"
           },
           {
               "kty": "EC",
               "alg": "ES512",
               "x": "Abijiex7rz7t-_Zj_E6Oo0OXe9C_-MCSD-OWio15ATQGjH9WpbWjN62ZqrrU_nwJiqqwx6ZsYKhUc_J3PRaMbdVC",
               "y": "FxaljCIuoVEA7PJIaDPJ5ePXtZ0hkinT1B_bQ91mShCiR_43Whsn1P7Gz30WEnLuJs1SGVz1oT4lIRUYni2OfIk"
           }
        ]
     }
)";

  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 3);

  EXPECT_EQ(jwks->keys()[0]->alg_, "ES256");
  EXPECT_EQ(jwks->keys()[0]->crv_, "P-256");

  EXPECT_EQ(jwks->keys()[1]->alg_, "ES384");
  EXPECT_EQ(jwks->keys()[1]->crv_, "P-384");

  EXPECT_EQ(jwks->keys()[2]->alg_, "ES512");
  EXPECT_EQ(jwks->keys()[2]->crv_, "P-521");
}

TEST(JwksParseTest, JwksOKPKeyBadAlg) {
  // OKP alg doesn't match with kty
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "OKP",
               "alg": "ES256"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksOKPKeyBadAlg);
}

TEST(JwksParseTest, JwksOKPKeyMissingCrv) {
  // OKP crv is missing
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "OKP",
               "alg": "EdDSA"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksOKPKeyMissingCrv);
}

TEST(JwksParseTest, JwksOKPKeyBadCrv) {
  // OKP crv is wrong type (not a string)
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "OKP",
               "alg": "EdDSA",
               "crv": 0
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksOKPKeyBadCrv);
}

TEST(JwksParseTest, JwksOKPKeyCrvUnsupported) {
  // OKP crv is unsupported
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "OKP",
               "alg": "EdDSA",
               "crv": "Ed448"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksOKPKeyCrvUnsupported);
}

TEST(JwksParseTest, JwksOKPKeyMissingX) {
  // OKP x is missing
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "OKP",
               "alg": "EdDSA",
               "crv": "Ed25519"
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksOKPKeyMissingX);
}

TEST(JwksParseTest, JwksOKPKeyBadX) {
  // OKP x is wrong type (not a string)
  const std::string jwks_text = R"(
     {
        "keys": [
           {
               "kty": "OKP",
               "alg": "EdDSA",
               "crv": "Ed25519",
               "x": 0
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksOKPKeyBadX);
}

TEST(JwksParseTest, JwksGoodX509) {
  auto jwks = Jwks::createFrom(kPublicKeyX509, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);

  EXPECT_EQ(jwks->keys().size(), 2);

  std::set<std::string> kids = {"62a93512c9ee4c7f8067b5a216dade2763d32a47",
                                "b3319a147514df7ee5e4bcdee51350cc890cc89e"};
  EXPECT_TRUE(kids.find(jwks->keys()[0]->kid_) != kids.end());
  EXPECT_TRUE(kids.find(jwks->keys()[1]->kid_) != kids.end());
}

TEST(JwksParseTest, RealJwksX509) {
  auto jwks = Jwks::createFrom(kRealX509Jwks, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 5);
}

TEST(JwksParseTest, JwksX509WrongTypeArray) {
  const std::string jwks_text = R"(
     {
        "kid1": [
           {
               "kty": "EC",
               "alg": "ES1024",
           }
        ]
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksNoKeys);
}

TEST(JwksParseTest, JwksX509WrongTypeBool) {
  const std::string jwks_text = R"(
     {
        "kid1": "pubkey1",
        "kid2": true
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksNoKeys);
}

TEST(JwksParseTest, JwksX509EmptyPubkey) {
  const std::string jwks_text = R"(
     {
        "kid1": "pubkey1",
        "kid2": ""
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksNoKeys);
}

TEST(JwksParseTest, JwksX509EmptyKid) {
  const std::string jwks_text = R"(
     {
        "": "pubkey1",
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksNoKeys);
}

TEST(JwksParseTest, JwksX509NotSuffixPrefix) {
  const std::string jwks_text = R"(
     {
        "kid1": "pubkey1",
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksNoKeys);
}

TEST(JwksParseTest, JwksX509NotSuffix) {
  const std::string jwks_text = R"(
     {
        "kid1": "-----BEGIN CERTIFICATE-----\npubkey1",
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksNoKeys);
}

TEST(JwksParseTest, JwksX509NotPrefix) {
  const std::string jwks_text = R"(
     {
        "kid1": "pubkey1\n-----END CERTIFICATE-----\n",
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksNoKeys);
}

TEST(JwksParseTest, JwksX509WrongPubkey) {
  const std::string jwks_text = R"(
     {
        "kid1": "-----BEGIN CERTIFICATE-----\nwrong-pubkey\n-----END CERTIFICATE-----\n",
     }
)";
  auto jwks = Jwks::createFrom(jwks_text, Jwks::JWKS);
  EXPECT_EQ(jwks->getStatus(), Status::JwksX509ParseError);
}

TEST(JwksParseTest, goodPEMRSA) {
  const std::string pem_text = R"(
-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAzUPYX/CJFCPg5fDfnTsV
6J0Lq2zMqCIj0/2taAsQm7sqrc5SCIeiDXypNzYYqshScbHPEfyj4egEqMMf9its
WY4khLWHcAd23ICHPdbga0YP4z+VTOkIMEpmJ8Oat68oeBaYhTMW1jr+9A2N/U/w
1AnketucyFFk0bkkmGuOefytbuBoxA2mkM+ZBVFRCXeiWq4LjgHZNpMNZ9Dz30Jk
6E+A0y2cMje4x6zMfulDf1ED6FN2LHqNE6uScFo5YL3tnvqMhkjJFMIzdvK4MWWh
2uTclOhgCH5rA6wQO2vWH8RRewaEfF0ihtg1WafSrcWK2MPDFI9/XhwzkBPBCG9l
ZQIDAQAB
-----END PUBLIC KEY-----
)";
  auto jwks = Jwks::createFrom(pem_text, Jwks::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 1);
}

TEST(JwksParseTest, goodPEMEC) {
  const std::string pem_text = R"(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEYaOv1HVESfIWB6jnkijUTPKvwkFu
CQnMe3gk4tp4DhYBSzTl6UXz9iRj15FMlmQpl9fV5nBfZMoUm47EkO7uaQ==
-----END PUBLIC KEY-----
)";
  auto jwks = Jwks::createFrom(pem_text, Jwks::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 1);
}

TEST(JwksParseTest, goodPEMEd25519) {
  const std::string pem_text = R"(
-----BEGIN PUBLIC KEY-----
MCowBQYDK2VwAyEAvWNRcLk4e4v62xnQqR+EksR7CHYdLQhFfFJibL1gYGA=
-----END PUBLIC KEY-----
)";
  auto jwks = Jwks::createFrom(pem_text, Jwks::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 1);
}

TEST(JwksParseTest, PemWrongHeader) {
  const std::string pem_text = R"(
-----BEGIN CERTIFICATE KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAzUPYX/CJFCPg5fDfnTsV
6J0Lq2zMqCIj0/2taAsQm7sqrc5SCIeiDXypNzYYqshScbHPEfyj4egEqMMf9its
WY4khLWHcAd23ICHPdbga0YP4z+VTOkIMEpmJ8Oat68oeBaYhTMW1jr+9A2N/U/w
1AnketucyFFk0bkkmGuOefytbuBoxA2mkM+ZBVFRCXeiWq4LjgHZNpMNZ9Dz30Jk
6E+A0y2cMje4x6zMfulDf1ED6FN2LHqNE6uScFo5YL3tnvqMhkjJFMIzdvK4MWWh
2uTclOhgCH5rA6wQO2vWH8RRewaEfF0ihtg1WafSrcWK2MPDFI9/XhwzkBPBCG9l
ZQIDAQAB
-----END CERTIFICATE KEY-----
)";
  auto jwks = Jwks::createFrom(pem_text, Jwks::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::JwksPemBadBase64);
}

TEST(JwksParseTest, PemInvalidKey) {
  const std::string pem_text = R"(
-----BEGIN PUBLIC KEY-----
bad-pub-key
-----END PUBLIC KEY-----
)";
  auto jwks = Jwks::createFrom(pem_text, Jwks::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::JwksPemBadBase64);
}

TEST(JwksParseTest, PemDsaUnimplimented) {
  const std::string pem_text = R"(
-----BEGIN PUBLIC KEY-----
MIIDRjCCAjkGByqGSM44BAEwggIsAoIBAQDWMfB0ccDLpds14iKIKMu/O0WgIjHu
yvUvDtnzdwiMDxOlbs5SkB2dFDUPRO+WNCuSJgYGsIgVBnHUEwTRm8jqXrjUoVRN
Vj5eQO9/UXit/Kadt1lCHlUeVqeA8KAApN/gbr1y0hrkMyHNDpjznR3/z8uc0Za9
iRkB4R0YDyGnrGI58WKuiOsSJpc7XOHHKhHFoXYy/g/De5pfV9d5s4FYjJNi2Ew8
SdWy2gztUnf3BrIbwWSUXNtm2W+zql9qTCFwXMEArKYwlk/6au0tMCW2/rfUAfYU
9Y9Vgi5rgEW3I/YV7mgYzw5sWFgj8wxEbUcvNM7iqgh/w054ZesTz58jAiEA7FKX
3tTqBtTTiUYVHXjaPUBiAAQevbFEMr4dVo0os2ECggEAEdhxaTqMQ2Wb625cDaWI
2OLpOXot2RTMSGQNKGi05OgsAKw6yVjwJuqqEdZi6XCtZ/SNUEZA8zmUyhdjj7ht
SeM+Km3b2M+FjLm7Wtvgl2QjiLmKhZKTrlZETs18aTkS8OrU5S6w2LDzOtZ6T7Ap
/A9tPf1F4CHnfykYmYDWcenZPhZHD/pv1ovSi5u7GNtvp1R2EsMV0+Pp0PwmSyX2
RAGjkSGyEtDjaXHy2Wh7b5BsfO2ixJb+6m8eBGaLxCZ3Su16R9C1xQ/lFHj6HPTV
3QvjayxaVVf3BjJgDaZX7b9gWuWhkP4eJ8M/xlfE2lJprl2RaDeZvpa22lP5Lcor
GgOCAQUAAoIBADk+JlpQuV2D0yMnS5ewzkiU5KjcwSWgTrw4KLWRFFfYWtdHy/Ot
xaafLzA04QM6Jh4q+iOJVhk2toxjW2+/6lYbmest83VPKGAaPs49gmWOVvU2gExp
MobhZpB4uwTUwanooCYOt5pV2Ysw8iOYI7H84L02yJJDFcv9qJJaw6+ZzZoSVE5q
17w7KTdUcvO46dDddIAknS1th2YrzFOj6syy56Y0nozMBgT6IQbbKD3WEWGc29Qw
+2/C+wusfP/gWpG6yCPpKXDLIWv583H+CoXD54dyJ3xH+c1UeDm+/pAM/oBynFFj
9y24N/KIm3v5f4Fb1v3v/by0kcfcg6vkRiQ=
-----END PUBLIC KEY-----
)";
  auto jwks = Jwks::createFrom(pem_text, Jwks::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::JwksPemNotImplementedKty);
}

TEST(JwksParseTest, CreateFromPemError) {
  const std::string pem_text = R"(
-----BEGIN CERTIFICATE KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAzUPYX/CJFCPg5fDfnTsV
6J0Lq2zMqCIj0/2taAsQm7sqrc5SCIeiDXypNzYYqshScbHPEfyj4egEqMMf9its
WY4khLWHcAd23ICHPdbga0YP4z+VTOkIMEpmJ8Oat68oeBaYhTMW1jr+9A2N/U/w
1AnketucyFFk0bkkmGuOefytbuBoxA2mkM+ZBVFRCXeiWq4LjgHZNpMNZ9Dz30Jk
6E+A0y2cMje4x6zMfulDf1ED6FN2LHqNE6uScFo5YL3tnvqMhkjJFMIzdvK4MWWh
2uTclOhgCH5rA6wQO2vWH8RRewaEfF0ihtg1WafSrcWK2MPDFI9/XhwzkBPBCG9l
ZQIDAQAB
-----END CERTIFICATE KEY-----
)";
  auto jwks = Jwks::createFromPem(pem_text, "", "");
  EXPECT_EQ(jwks->getStatus(), Status::JwksPemBadBase64);
}

TEST(JwksParseTest, CreateFromPemPopulatesExpectedFields) {
  const std::string pem_text = R"(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEYaOv1HVESfIWB6jnkijUTPKvwkFu
CQnMe3gk4tp4DhYBSzTl6UXz9iRj15FMlmQpl9fV5nBfZMoUm47EkO7uaQ==
-----END PUBLIC KEY-----
)";
  auto jwks = Jwks::createFromPem(pem_text, "kid1", "ES256");
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 1);
  EXPECT_EQ(jwks->keys().at(0)->kid_, "kid1");
  EXPECT_EQ(jwks->keys().at(0)->alg_, "ES256");
  EXPECT_EQ(jwks->keys().at(0)->crv_, "P-256");
}

TEST(JwksParseTest, addKeyFromPemSuccess) {
  const std::string pem_text = R"(
-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAzUPYX/CJFCPg5fDfnTsV
6J0Lq2zMqCIj0/2taAsQm7sqrc5SCIeiDXypNzYYqshScbHPEfyj4egEqMMf9its
WY4khLWHcAd23ICHPdbga0YP4z+VTOkIMEpmJ8Oat68oeBaYhTMW1jr+9A2N/U/w
1AnketucyFFk0bkkmGuOefytbuBoxA2mkM+ZBVFRCXeiWq4LjgHZNpMNZ9Dz30Jk
6E+A0y2cMje4x6zMfulDf1ED6FN2LHqNE6uScFo5YL3tnvqMhkjJFMIzdvK4MWWh
2uTclOhgCH5rA6wQO2vWH8RRewaEfF0ihtg1WafSrcWK2MPDFI9/XhwzkBPBCG9l
ZQIDAQAB
-----END PUBLIC KEY-----
)";
  auto jwks = Jwks::createFrom(pem_text, Jwks::PEM);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);

  const std::string pem_text2 = R"(
-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEA4XGAA932qKSXzHIFfxiS
VB0ZKKwPyg+LrIcTQnDH7XKcB6mkyRtcnPqXSFW21oi+m7uvSShhC1T1oYoYJqb8
wzF249CPsj3a5h5cbyXfD9e0no+XDuMvuegPF7zCaVA1r6cNY6l66JtQpRC6LkT8
xNajblo7/MCI0sky2S0q/V7BO3oOQAljNALIoPzc1N4bAMk2qL91Fs71gUj55Fvc
d9i2BqBOzGLyq55aRkFsJh5pqDJTv0gWRgcgDPjen35hRHme0DfumfV2sKjvUayw
seWc4K+j8vzyg/qn5bUE4Zbk3FksmK+hh3/btzRySD5sfHEv1MONWl+DYDCLNMlD
WwIDAQAB
-----END PUBLIC KEY-----
)";
  Status status = jwks->addKeyFromPem(pem_text2, "kid2", "RS256");
  EXPECT_EQ(status, Status::Ok);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 2);
  EXPECT_EQ(jwks->keys().at(0)->kid_, "");
  EXPECT_EQ(jwks->keys().at(1)->kid_, "kid2");
  EXPECT_EQ(jwks->keys().at(1)->crv_, "");
}

TEST(JwksParseTest, addKeyFromPemError) {
  const std::string good_pem_text = R"(
-----BEGIN PUBLIC KEY-----
MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEYaOv1HVESfIWB6jnkijUTPKvwkFu
CQnMe3gk4tp4DhYBSzTl6UXz9iRj15FMlmQpl9fV5nBfZMoUm47EkO7uaQ==
-----END PUBLIC KEY-----
)";
  auto jwks = Jwks::createFromPem(good_pem_text, "kid1", "ES256");
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 1);

  const std::string pem_text = R"(
-----BEGIN PUBLIC KEY-----
bad-pub-key
-----END PUBLIC KEY-----
)";
  Status status = jwks->addKeyFromPem(pem_text, "kid1", "EC256");
  EXPECT_EQ(status, Status::JwksPemBadBase64);
  EXPECT_EQ(jwks->getStatus(), Status::Ok);
  EXPECT_EQ(jwks->keys().size(), 1);
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy
