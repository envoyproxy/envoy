// Copyright 2018 Google LLC
// Copyright Envoy Project Authors
// SPDX-License-Identifier: Apache-2.0

#include "source/common/jwt/verify.h"

#include "test/common/jwt/test_common.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace JwtVerify {
namespace {

// The following is the jwks from querying a private temporary instance of
// keycloak at
// https://keycloak.localhost/auth/realms/applications/protocol/openid-connect/certs

const std::string PublicKeyRSAPSS = R"(
{
  "keys": [
    {
      "kid": "RGlV9a54XdAsuiYUDkQ0hDkiSZ92TJCgneh7-HvN-sk",
      "kty": "RSA",
      "alg": "PS384",
      "use": "sig",
      "n": "8logDcIilAXYJ2kNOrUIAVrWg3g-i1EUsWzEwAV3WT9NNwisUsljdyK3OOxy8yhbWyunxia-4Qo8nCIjURfLn0XoJyozCsruTWuvv2nvWx380zDD5gN-RK0kab_UWOV_zkr9YhBYd2PUB-sCcEwDKj8uHZrJ2CvXvxt2LV8_l_kwlCEDS_q97eEqvxhvYFF8DVo_AGABoK6fU1urn7X-GQcClgOEI8qKho-FU0RPJM80pnmCVds7oP2NYHSnAbkxltiB2cU1qazs21A52obU5zemUwJcdEGpykBKgc_aKaxkusLs2O0xWvnDbgXvboqb_0UhZPWNILZYK09jYCFobQ",
      "e": "AQAB",
      "x5c": [
        "MIICpzCCAY8CBgFzHKZh6TANBgkqhkiG9w0BAQsFADAXMRUwEwYDVQQDDAxhcHBsaWNhdGlvbnMwHhcNMjAwNzA1MDE0MzUyWhcNMzAwNzA1MDE0NTMyWjAXMRUwEwYDVQQDDAxhcHBsaWNhdGlvbnMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDyWiANwiKUBdgnaQ06tQgBWtaDeD6LURSxbMTABXdZP003CKxSyWN3Irc47HLzKFtbK6fGJr7hCjycIiNRF8ufRegnKjMKyu5Na6+/ae9bHfzTMMPmA35ErSRpv9RY5X/OSv1iEFh3Y9QH6wJwTAMqPy4dmsnYK9e/G3YtXz+X+TCUIQNL+r3t4Sq/GG9gUXwNWj8AYAGgrp9TW6uftf4ZBwKWA4QjyoqGj4VTRE8kzzSmeYJV2zug/Y1gdKcBuTGW2IHZxTWprOzbUDnahtTnN6ZTAlx0QanKQEqBz9oprGS6wuzY7TFa+cNuBe9uipv/RSFk9Y0gtlgrT2NgIWhtAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAMaFjwzA+74wY+2YjsMk79IpvDV3Kke7hBThz+9+KT8u2cCX1fUucZemk5vNfLbv+Swhjs+Psuhim1mXxqfyNeSPrIznWAQDSUIW5c3SuJtIOXbfXjIoeK7QW4yhv4NsQBnXd0o6UncvlSZvFxQCMDqGrybOim2O93nM7p3udE2c08tAZ/XRFrxgENvuO3XGAg5EIiUEbHjtOgpjGwkxDfvOm0C4giaaHbUEarzK0olAExtKENwa9AKsxnckMH/kWNBY6ohYSJ7DojRUY84bKTWWFx8Krj0kzjNkbadrdAya8YoRp4IRqjZ9cA9i+yIlN1ulhL9GGq4JDHqTFaoBxiQ="
      ],
      "x5t": "6mK6ZUgfCVv2sm7GVsDR_tdPjjE",
      "x5t#S256": "PJYSXCbyowmimYVC41vPKlZyUfmqcGNo6Cfba4y8pkE"
    },
    {
      "kid": "u_ZZAorrQhtL2MA-bWkZ0qpzjia4D3u6QUvBRscHLrg",
      "kty": "RSA",
      "alg": "PS512",
      "use": "sig",
      "n": "0k2d9uo6k1luw7VpgeZuf4xIlhpp_pPndYjHCZBhSmXsXN7lV-HhYE3Vv2WurMT32HrOJVm4zJWbQOOFG2LD8Byw1sKzZWoS_wwFUWdeTzw43JniK-PYDY5sOM5sn6uGtfLNzm0fO0gkhLMf-dgodimA7dw_4kFqIYP9VNJOi3Pw3XI0uAuK1X7_eJ7mzWlCC8ERT0iJELKqC1Hx8Ub13SeTaFvPoguvx08END87WUbkdp4e4N16d_wVUWuutidY2HkjcklNhUWTc0BSST89TyKwwXwrXqY7_Ka14pjo8H-s6nT1ns80LiTjvjgzyeMRbptOYmgxlmYL0AXI07hbZw",
      "e": "AQAB",
      "x5c": [
        "MIICpzCCAY8CBgFzHKaU5jANBgkqhkiG9w0BAQsFADAXMRUwEwYDVQQDDAxhcHBsaWNhdGlvbnMwHhcNMjAwNzA1MDE0NDA1WhcNMzAwNzA1MDE0NTQ1WjAXMRUwEwYDVQQDDAxhcHBsaWNhdGlvbnMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQDSTZ326jqTWW7DtWmB5m5/jEiWGmn+k+d1iMcJkGFKZexc3uVX4eFgTdW/Za6sxPfYes4lWbjMlZtA44UbYsPwHLDWwrNlahL/DAVRZ15PPDjcmeIr49gNjmw4zmyfq4a18s3ObR87SCSEsx/52Ch2KYDt3D/iQWohg/1U0k6Lc/DdcjS4C4rVfv94nubNaUILwRFPSIkQsqoLUfHxRvXdJ5NoW8+iC6/HTwQ0PztZRuR2nh7g3Xp3/BVRa662J1jYeSNySU2FRZNzQFJJPz1PIrDBfCtepjv8prXimOjwf6zqdPWezzQuJOO+ODPJ4xFum05iaDGWZgvQBcjTuFtnAgMBAAEwDQYJKoZIhvcNAQELBQADggEBALyEXqK3BYwVU/7o8+wfDwOJ2nk9OmoGIu3hu6gwqC92DOXClus11UGHKsfhhYlQdzdBpNRD5hjabqaEpCoqF4HwzIWL+Pc08hnnP1IsxkAZdKicKLeFE6BwldK/RYK5vuNXjO824xGnTJuIEApWD2lWf7T3Ndyve14vx1B+6NPmazXPHcSbDN+06bXg8YeZVMnBqRYVBCxo5IoEwP2kJC/F3RbYJTF8QV2/AnwA/Bt1/rl6Y9MPqCwntyfrxq26Bwlpf9vC1dwRK45Tgv9c94/rD1Xax3MPQhhnCo+6H9UWSe/mIdPC2jPifcYJGujPpbbcp23fBOig+FwY6OZl1oo="
      ],
      "x5t": "YVSZ0gbRsdQ2ItVwc00GynAyFwk",
      "x5t#S256": "ZOJz7HKW1fQVb46QI0Ymw7v4u1mfRmzDJmOp3zUMpt4"
    },
    {
      "kid": "4hmO65bbc7IVI-3PfA2emAlO0qhv4rB__yw8BPQ58q8",
      "kty": "RSA",
      "alg": "PS256",
      "use": "sig",
      "n": "vz40nPlC2XsAGbqfp3S4nyl2G1iMFER1l_I4k7gfC-87UWu2-a7BZQHb646WmSXu8xFzu0x5FFTFmu_v3Aj1NAcdYbz09UypSxfH--aw7ATiSWL26jHixFP4l6miJxaXV-rlp9qFSO--1JRnlvYrt6M5mQI0ZvN8EahAVXIHNtDMZYu0HYwwL7j45gjF9o9kDbfMSPr8Oni0QC2tTcCg623OlNqrJZFT4YNJ8A1nRfwGwBLFp5pxpK9ZCekQVhBpZNUrlLB5uDaB5H9lwFKslbHC-HKlJbfZZg16j6tlQTgw6dnKNo5LPrZ4TeSUyuoudzZSpZo4dyFsasTfWYTSLQ",
      "e": "AQAB",
      "x5c": [
        "MIICpzCCAY8CBgFzHIdU1jANBgkqhkiG9w0BAQsFADAXMRUwEwYDVQQDDAxhcHBsaWNhdGlvbnMwHhcNMjAwNzA1MDEwOTU3WhcNMzAwNzA1MDExMTM3WjAXMRUwEwYDVQQDDAxhcHBsaWNhdGlvbnMwggEiMA0GCSqGSIb3DQEBAQUAA4IBDwAwggEKAoIBAQC/PjSc+ULZewAZup+ndLifKXYbWIwURHWX8jiTuB8L7ztRa7b5rsFlAdvrjpaZJe7zEXO7THkUVMWa7+/cCPU0Bx1hvPT1TKlLF8f75rDsBOJJYvbqMeLEU/iXqaInFpdX6uWn2oVI777UlGeW9iu3ozmZAjRm83wRqEBVcgc20Mxli7QdjDAvuPjmCMX2j2QNt8xI+vw6eLRALa1NwKDrbc6U2qslkVPhg0nwDWdF/AbAEsWnmnGkr1kJ6RBWEGlk1SuUsHm4NoHkf2XAUqyVscL4cqUlt9lmDXqPq2VBODDp2co2jks+tnhN5JTK6i53NlKlmjh3IWxqxN9ZhNItAgMBAAEwDQYJKoZIhvcNAQELBQADggEBAEhiswSA4BBd9ka473JMX27y+4ZyxitUWi9ARhloiPtE7b+HVsRd6febjPlwMZJ/x7c5lRrQXEGCtJdHcVf2JybNo9bAPSsnmGAD9I+x5GyJljgRuItcfIJ3ALV7LqMbFPZ7cO6jB9hzYtjzECRN0+hJKSZm99kpau2sI8C1FkT+aSK7+j0jGagYwfI8hG7SV1IKQgTxtGZSpFgn2mi60TYsnLt2JYKSACq5hZykO7BPxnTK0sAK9ue34ddEuVe6L1wxDv44PME2dZwRmCRT5d7qj8lO4n2VYqBbc90ME6yAeRIhYRZSrHFTE2Wkufi+21HXIB63dKoYqiPe3y/GZno="
      ],
      "x5t": "5lmEYc56y8EeBpHsP1-LO8M0W2c",
      "x5t#S256": "oC0EpmLVEv1CptAVxKT9uVpC975xKlu3xOrhh8RTNy4"
    }
  ]
}
)";

// SPELLCHECKER(off)
// PS256 JWT with correct kid
// Header:
// {
//   "alg": "PS256",
//   "typ": "JWT",
//   "kid": "4hmO65bbc7IVI-3PfA2emAlO0qhv4rB__yw8BPQ58q8"
// }
// Payload:
// {
//   "exp": 1593912811,
//   "iat": 1593912511,
//   "jti": "3c9ee909-3ca5-4587-8c0b-700cb4cb8e62",
//   "iss": "https://keycloak.localhost/auth/realms/applications",
//   "sub": "c3cfd999-ca22-4080-9863-277427db4321",
//   "typ": "Bearer",
//   "azp": "foo",
//   "session_state": "de37ba9c-4b3a-4250-a89b-da81928fcf9b",
//   "acr": "1",
//   "scope": "email profile",
//   "email_verified": false,
//   "name": "User Zero",
//   "preferred_username": "user0",
//   "given_name": "User",
//   "family_name": "Zero",
//   "email": "user0@mail.com"
// }
// SPELLCHECKER(on)

const std::string Ps256JwtTextWithCorrectKid =
    "eyJhbGciOiJQUzI1NiIsInR5cCIgOiAiSldUIiwia2lkIiA6ICI0aG1PNjViYmM3SVZJLTNQ"
    "ZkEyZW1BbE8wcWh2NHJCX195dzhCUFE1OHE4In0."
    "eyJleHAiOjE1OTM5MTI4MTEsImlhdCI6MTU5MzkxMjUxMSwianRpIjoiM2M5ZWU5MDktM2Nh"
    "NS00NTg3LThjMGItNzAwY2I0Y2I4ZTYyIiwiaXNzIjoiaHR0cHM6Ly9rZXljbG9hay5sb2Nh"
    "bGhvc3QvYXV0aC9yZWFsbXMvYXBwbGljYXRpb25zIiwic3ViIjoiYzNjZmQ5OTktY2EyMi00"
    "MDgwLTk4NjMtMjc3NDI3ZGI0MzIxIiwidHlwIjoiQmVhcmVyIiwiYXpwIjoiZm9vIiwic2Vz"
    "c2lvbl9zdGF0ZSI6ImRlMzdiYTljLTRiM2EtNDI1MC1hODliLWRhODE5MjhmY2Y5YiIsImFj"
    "ciI6IjEiLCJzY29wZSI6ImVtYWlsIHByb2ZpbGUiLCJlbWFpbF92ZXJpZmllZCI6ZmFsc2Us"
    "Im5hbWUiOiJVc2VyIFplcm8iLCJwcmVmZXJyZWRfdXNlcm5hbWUiOiJ1c2VyMCIsImdpdmVu"
    "X25hbWUiOiJVc2VyIiwiZmFtaWx5X25hbWUiOiJaZXJvIiwiZW1haWwiOiJ1c2VyMEBtYWls"
    "LmNvbSJ9."
    "fas6TkXZ97K1d8tTMCEFDcG-MupI-BwGn0UZD8riwmbLf5xmDPaoZwmJ3k-szVo-oJMfMZbr"
    "VAI8xQwg4Z7bQvd3I9WM6XPsu1_gKnkc2EOATgkdpDg5rWOPSZCFLUD_bqsoPQrfc2C1-UKs"
    "VOwUkXEH6rEIlOvngqQWNJjtbkvsS2N_3kNAgaD8cELT5mxmM4vGZn14OHmXHJBIW9pHJU64"
    "tA0sDcexoylL7xB_E1XTs3St0sYyq_pz9920vHScr9KXQ3y9k-fbPvgBs2gGY0iK63E0lEwD"
    "fRWY4Za6RRqymammehv7ZiE4HjDy5Q_AdLGdRefrTxtiQrHIThLqAw";

// SPELLCHECKER(off)
// PS384 JWT with correct kid
// Header:
// {
//   "alg": "PS384",
//   "typ": "JWT",
//   "kid": "RGlV9a54XdAsuiYUDkQ0hDkiSZ92TJCgneh7-HvN-sk"
// }
// Payload:
// {
//   "exp": 1593913901,
//   "iat": 1593913601,
//   "jti": "375242be-54c3-4c06-ad07-22457d493390",
//   "iss": "https://keycloak.localhost/auth/realms/applications",
//   "sub": "c3cfd999-ca22-4080-9863-277427db4321",
//   "typ": "Bearer",
//   "azp": "foo",
//   "session_state": "a0cc48a5-1eea-4078-b965-3f8edee8a15e",
//   "acr": "1",
//   "scope": "email profile",
//   "email_verified": false,
//   "name": "User Zero",
//   "preferred_username": "user0",
//   "given_name": "User",
//   "family_name": "Zero",
//   "email": "user0@mail.com"
// }
// SPELLCHECKER(on)

const std::string Ps384JwtTextWithCorrectKid =
    "eyJhbGciOiJQUzM4NCIsInR5cCIgOiAiSldUIiwia2lkIiA6ICJSR2xWOWE1NFhkQXN1aVlV"
    "RGtRMGhEa2lTWjkyVEpDZ25laDctSHZOLXNrIn0."
    "eyJleHAiOjE1OTM5MTM5MDEsImlhdCI6MTU5MzkxMzYwMSwianRpIjoiMzc1MjQyYmUtNTRj"
    "My00YzA2LWFkMDctMjI0NTdkNDkzMzkwIiwiaXNzIjoiaHR0cHM6Ly9rZXljbG9hay5sb2Nh"
    "bGhvc3QvYXV0aC9yZWFsbXMvYXBwbGljYXRpb25zIiwic3ViIjoiYzNjZmQ5OTktY2EyMi00"
    "MDgwLTk4NjMtMjc3NDI3ZGI0MzIxIiwidHlwIjoiQmVhcmVyIiwiYXpwIjoiZm9vIiwic2Vz"
    "c2lvbl9zdGF0ZSI6ImEwY2M0OGE1LTFlZWEtNDA3OC1iOTY1LTNmOGVkZWU4YTE1ZSIsImFj"
    "ciI6IjEiLCJzY29wZSI6ImVtYWlsIHByb2ZpbGUiLCJlbWFpbF92ZXJpZmllZCI6ZmFsc2Us"
    "Im5hbWUiOiJVc2VyIFplcm8iLCJwcmVmZXJyZWRfdXNlcm5hbWUiOiJ1c2VyMCIsImdpdmVu"
    "X25hbWUiOiJVc2VyIiwiZmFtaWx5X25hbWUiOiJaZXJvIiwiZW1haWwiOiJ1c2VyMEBtYWls"
    "LmNvbSJ9."
    "lQdbyqQH0dBYA0yIMVmV-KMGOYc7-BuuQUggKqEi9kpmvZAeXaX1v04n6XkyZdIRMxLgxVoK"
    "LH3XJLg7zwW_luYR5ZlYj5SLYxUSkrlG3RfOvRpphXzhH-TcRQMdwSFEbNUiibZ6NkSmzMLi"
    "Weryi3JHCHAxt2e9Z6_dWlrKXXSvpmZgrn--NdU433TmePFdgoEGUH8F9q7T1Nd1S5FnsS2i"
    "-ywZzNMQIfQ59k_r1_WlH81bwoNgd4ffTlVsosZrw84UYBJdNt73-RWu1NNTXvIY2MiImods"
    "oo7DAD__ZDMgnJ8cpBmrq0YASz04SESNt1jiwCWbasJQx_B73hmd1A";

// SPELLCHECKER(off)
// PS512 JWT with correct kid
// Header:
// {
//   "alg": "PS512",
//   "typ": "JWT",
//   "kid": "u_ZZAorrQhtL2MA-bWkZ0qpzjia4D3u6QUvBRscHLrg"
// }
// Payload:
// {
//   "exp": 1593913918,
//   "iat": 1593913618,
//   "jti": "7c1f8cba-7f7c-4e05-b02c-2a0a77914f5d",
//   "iss": "https://keycloak.localhost/auth/realms/applications",
//   "sub": "c3cfd999-ca22-4080-9863-277427db4321",
//   "typ": "Bearer",
//   "azp": "foo",
//   "session_state": "d8dbe685-cd10-42da-841c-f7ae6cd4d588",
//   "acr": "1",
//   "scope": "email profile",
//   "email_verified": false,
//   "name": "User Zero",
//   "preferred_username": "user0",
//   "given_name": "User",
//   "family_name": "Zero",
//   "email": "user0@mail.com"
// }
// SPELLCHECKER(on)

const std::string Ps512JwtTextWithCorrectKid =
    "eyJhbGciOiJQUzUxMiIsInR5cCIgOiAiSldUIiwia2lkIiA6ICJ1X1paQW9yclFodEwyTUEt"
    "YldrWjBxcHpqaWE0RDN1NlFVdkJSc2NITHJnIn0."
    "eyJleHAiOjE1OTM5MTM5MTgsImlhdCI6MTU5MzkxMzYxOCwianRpIjoiN2MxZjhjYmEtN2Y3"
    "Yy00ZTA1LWIwMmMtMmEwYTc3OTE0ZjVkIiwiaXNzIjoiaHR0cHM6Ly9rZXljbG9hay5sb2Nh"
    "bGhvc3QvYXV0aC9yZWFsbXMvYXBwbGljYXRpb25zIiwic3ViIjoiYzNjZmQ5OTktY2EyMi00"
    "MDgwLTk4NjMtMjc3NDI3ZGI0MzIxIiwidHlwIjoiQmVhcmVyIiwiYXpwIjoiZm9vIiwic2Vz"
    "c2lvbl9zdGF0ZSI6ImQ4ZGJlNjg1LWNkMTAtNDJkYS04NDFjLWY3YWU2Y2Q0ZDU4OCIsImFj"
    "ciI6IjEiLCJzY29wZSI6ImVtYWlsIHByb2ZpbGUiLCJlbWFpbF92ZXJpZmllZCI6ZmFsc2Us"
    "Im5hbWUiOiJVc2VyIFplcm8iLCJwcmVmZXJyZWRfdXNlcm5hbWUiOiJ1c2VyMCIsImdpdmVu"
    "X25hbWUiOiJVc2VyIiwiZmFtaWx5X25hbWUiOiJaZXJvIiwiZW1haWwiOiJ1c2VyMEBtYWls"
    "LmNvbSJ9."
    "p-NqE3q9BVakZNkKX3-X5FKIm64PloIjBjWfajQuRayHv4cj6xwvDve3uCuZa2oKyefJRNLy"
    "6rCJUGNsYM9Q-WRCtD6SuWLPkuqh-SUFtZqW7sWGOqTLKbMBx5StLZx7eEgdRWqzIxwLVLdF"
    "VuO-3L88qHFTU2Vv8UAu_nX-uyFKOV5bYgyFlxqgpSqvsbm6lZ0EZghPuidOmnMPQdS8-Evk"
    "jwSAYEgoQ1crXY8dEUc_AJfq84jtuMJMnFhfVQvk_8hN71wYWWYThXtEATFySUFrkoCvB-da"
    "Sl9FNeK5UPE9vYBi7QJ-Wt3Ikg7kEgPiuADlIao_ZxKdzoA51isGBg";

class VerifyJwkRsaPssTest : public testing::Test {
protected:
  void SetUp() {
    jwks_ = Jwks::createFrom(PublicKeyRSAPSS, Jwks::Type::JWKS);
    EXPECT_EQ(jwks_->getStatus(), Status::Ok);
  }

  JwksPtr jwks_;
};

TEST_F(VerifyJwkRsaPssTest, Ps256CorrectKidOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(Ps256JwtTextWithCorrectKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);

  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

TEST_F(VerifyJwkRsaPssTest, Ps384CorrectKidOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(Ps384JwtTextWithCorrectKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);

  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

TEST_F(VerifyJwkRsaPssTest, Ps512CorrectKidOK) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(Ps512JwtTextWithCorrectKid), Status::Ok);
  EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::Ok);

  fuzzJwtSignature(jwt, [this](const Jwt& jwt) {
    EXPECT_EQ(verifyJwt(jwt, *jwks_, 1), Status::JwtVerificationFail);
  });
}

// SPELLCHECKER(off)
// This set of keys and jwts were generated at https://jwt.io/
// public key:
//     "-----BEGIN PUBLIC KEY-----"
//     "MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAnzyis1ZjfNB0bBgKFMSv"
//     "vkTtwlvBsaJq7S5wA+kzeVOVpVWwkWdVha4s38XM/pa/yr47av7+z3VTmvDRyAHc"
//     "aT92whREFpLv9cj5lTeJSibyr/Mrm/YtjCZVWgaOYIhwrXwKLqPr/11inWsAkfIy"
//     "tvHWTxZYEcXLgAXFuUuaS3uF9gEiNQwzGTU1v0FqkqTBr4B8nW3HCN47XUu0t8Y0"
//     "e+lf4s4OxQawWD79J9/5d3Ry0vbV3Am1FtGJiJvOwRsIfVChDpYStTcHTCMqtvWb"
//     "V6L11BWkpzGXSW4Hv43qa+GSYOD2QU68Mb59oSk2OB+BtOLpJofmbGEGgvmwyCI9"
//     "MwIDAQAB"
//     "-----END PUBLIC KEY-----"
// SPELLCHECKER(on)

const std::string JwtIoPublicKeyRSAPSS = R"(
{
  "keys": [
    {
      "kty": "RSA",
      "kid": "f08a1cc9-d266-4049-9c22-f95260cbf5fd",
      "e": "AQAB",
      "n": "nzyis1ZjfNB0bBgKFMSvvkTtwlvBsaJq7S5wA-kzeVOVpVWwkWdVha4s38XM_pa_yr47av7-z3VTmvDRyAHcaT92whREFpLv9cj5lTeJSibyr_Mrm_YtjCZVWgaOYIhwrXwKLqPr_11inWsAkfIytvHWTxZYEcXLgAXFuUuaS3uF9gEiNQwzGTU1v0FqkqTBr4B8nW3HCN47XUu0t8Y0e-lf4s4OxQawWD79J9_5d3Ry0vbV3Am1FtGJiJvOwRsIfVChDpYStTcHTCMqtvWbV6L11BWkpzGXSW4Hv43qa-GSYOD2QU68Mb59oSk2OB-BtOLpJofmbGEGgvmwyCI9Mw"
    }
  ]
}
)";

// SPELLCHECKER(off)
// private key:
//     "-----BEGIN RSA PRIVATE KEY-----"
//     "MIIEogIBAAKCAQEAnzyis1ZjfNB0bBgKFMSvvkTtwlvBsaJq7S5wA+kzeVOVpVWw"
//     "kWdVha4s38XM/pa/yr47av7+z3VTmvDRyAHcaT92whREFpLv9cj5lTeJSibyr/Mr"
//     "m/YtjCZVWgaOYIhwrXwKLqPr/11inWsAkfIytvHWTxZYEcXLgAXFuUuaS3uF9gEi"
//     "NQwzGTU1v0FqkqTBr4B8nW3HCN47XUu0t8Y0e+lf4s4OxQawWD79J9/5d3Ry0vbV"
//     "3Am1FtGJiJvOwRsIfVChDpYStTcHTCMqtvWbV6L11BWkpzGXSW4Hv43qa+GSYOD2"
//     "QU68Mb59oSk2OB+BtOLpJofmbGEGgvmwyCI9MwIDAQABAoIBACiARq2wkltjtcjs"
//     "kFvZ7w1JAORHbEufEO1Eu27zOIlqbgyAcAl7q+/1bip4Z/x1IVES84/yTaM8p0go"
//     "amMhvgry/mS8vNi1BN2SAZEnb/7xSxbflb70bX9RHLJqKnp5GZe2jexw+wyXlwaM"
//     "+bclUCrh9e1ltH7IvUrRrQnFJfh+is1fRon9Co9Li0GwoN0x0byrrngU8Ak3Y6D9"
//     "D8GjQA4Elm94ST3izJv8iCOLSDBmzsPsXfcCUZfmTfZ5DbUDMbMxRnSo3nQeoKGC"
//     "0Lj9FkWcfmLcpGlSXTO+Ww1L7EGq+PT3NtRae1FZPwjddQ1/4V905kyQFLamAA5Y"
//     "lSpE2wkCgYEAy1OPLQcZt4NQnQzPz2SBJqQN2P5u3vXl+zNVKP8w4eBv0vWuJJF+"
//     "hkGNnSxXQrTkvDOIUddSKOzHHgSg4nY6K02ecyT0PPm/UZvtRpWrnBjcEVtHEJNp"
//     "bU9pLD5iZ0J9sbzPU/LxPmuAP2Bs8JmTn6aFRspFrP7W0s1Nmk2jsm0CgYEAyH0X"
//     "+jpoqxj4efZfkUrg5GbSEhf+dZglf0tTOA5bVg8IYwtmNk/pniLG/zI7c+GlTc9B"
//     "BwfMr59EzBq/eFMI7+LgXaVUsM/sS4Ry+yeK6SJx/otIMWtDfqxsLD8CPMCRvecC"
//     "2Pip4uSgrl0MOebl9XKp57GoaUWRWRHqwV4Y6h8CgYAZhI4mh4qZtnhKjY4TKDjx"
//     "QYufXSdLAi9v3FxmvchDwOgn4L+PRVdMwDNms2bsL0m5uPn104EzM6w1vzz1zwKz"
//     "5pTpPI0OjgWN13Tq8+PKvm/4Ga2MjgOgPWQkslulO/oMcXbPwWC3hcRdr9tcQtn9"
//     "Imf9n2spL/6EDFId+Hp/7QKBgAqlWdiXsWckdE1Fn91/NGHsc8syKvjjk1onDcw0"
//     "NvVi5vcba9oGdElJX3e9mxqUKMrw7msJJv1MX8LWyMQC5L6YNYHDfbPF1q5L4i8j"
//     "8mRex97UVokJQRRA452V2vCO6S5ETgpnad36de3MUxHgCOX3qL382Qx9/THVmbma"
//     "3YfRAoGAUxL/Eu5yvMK8SAt/dJK6FedngcM3JEFNplmtLYVLWhkIlNRGDwkg3I5K"
//     "y18Ae9n7dHVueyslrb6weq7dTkYDi3iOYRW8HRkIQh06wEdbxt0shTzAJvvCQfrB"
//     "jg/3747WSsf/zBTcHihTRBdAv6OmdhV4/dD5YBfLAkLrd+mX7iE="
//     "-----END RSA PRIVATE KEY-----"
// SPELLCHECKER(on)

const std::string JwtTextWithNoKid =
    "eyJhbGciOiJQUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlh"
    "dCI6MTUxNjIzOTAyMn0."
    "hZnl5amPk_I3tb4O-Otci_5XZdVWhPlFyVRvcqSwnDo_srcysDvhhKOD01DigPK1lJvTSTol"
    "yUgKGtpLqMfRDXQlekRsF4XhAjYZTmcynf-C-6wO5EI4wYewLNKFGGJzHAknMgotJFjDi_NC"
    "VSjHsW3a10nTao1lB82FRS305T226Q0VqNVJVWhE4G0JQvi2TssRtCxYTqzXVt22iDKkXeZJ"
    "ARZ1paXHGV5Kd1CljcZtkNZYIGcwnj65gvuCwohbkIxAnhZMJXCLaVvHqv9l-AAUV7esZvkQ"
    "R1IpwBAiDQJh4qxPjFGylyXrHMqh5NlT_pWL2ZoULWTg_TJjMO9TuQ";

const std::string JwtTextWithNonExistentKid =
    "eyJhbGciOiJQUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6Im5vbmV4aXN0ZW50In0."
    "eyJzdWIiOiIxMjM0NTY3ODkwIiwibmFtZSI6IkpvaG4gRG9lIiwiYWRtaW4iOnRydWUsImlh"
    "dCI6MTUxNjIzOTAyMn0."
    "USMoL8XwVl-sqtIl-VQr97oNr1XWbgnJnDJbi65ExV7IioYQ3cGfrpi9n2GxJOwuw6zU572l"
    "ME-wD9It-Q8H8eAOi83KoimQJmdzCGGUGTgwo3tZK5HV7W3srgP1_46-X43DYWOT6h1pIAE7"
    "7s23XuSKbq4rpp6cmbDODARfTj6OTQWTqwhOkX0Xo7i2q1foreKI8PnOyrvbs7oXrLJGZhg_"
    "6mRnP0wRJJFkIu2uYKcLDcgJ0OWXY6dQ-8agj-yjZ5ZUX8GUcy347P0UUpsGVNd1pUawLwTi"
    "kmNidJOxkGlawLtOwE7u0WtZdYmcppx99Qw5U4gYdQQx0wJqgj_d8g";

// Expected behavior for `VerifyKidMatchingTest`:
// If kid is not specified in the jwt, allow verification as long as any of the
//   keys in the jwks are appropriate.
// If kid is specified in the jwt, use only the requested key in the jwks for
//   verification.
class VerifyKidMatchingTest : public testing::Test {
protected:
  void SetUp() {
    correct_jwks_ = Jwks::createFrom(JwtIoPublicKeyRSAPSS, Jwks::Type::JWKS);
    EXPECT_EQ(correct_jwks_->getStatus(), Status::Ok);
    wrong_jwks_ = Jwks::createFrom(PublicKeyRSAPSS, Jwks::Type::JWKS);
    EXPECT_EQ(wrong_jwks_->getStatus(), Status::Ok);
  }

  // This jwks contains the appropriate key for signature verification
  JwksPtr correct_jwks_;
  // This jwks does not contain the appropriate key for signature verification
  JwksPtr wrong_jwks_;
};

TEST_F(VerifyKidMatchingTest, JwtTextWithNoKidNoMatchingKey) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextWithNoKid), Status::Ok);
  // jwt has no kid, and none of the keys in the jwks can be used to verify,
  //   hence verification fails
  EXPECT_EQ(verifyJwt(jwt, *wrong_jwks_), Status::JwtVerificationFail);
}

TEST_F(VerifyKidMatchingTest, JwtTextWithNoKidOk) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextWithNoKid), Status::Ok);
  // jwt has no kid, and one of the keys in the jwks can be used to verify,
  //   hence verification is ok
  EXPECT_EQ(verifyJwt(jwt, *correct_jwks_, 1), Status::Ok);
}

TEST_F(VerifyKidMatchingTest, JwtTextWithNonExistentKid) {
  Jwt jwt;
  EXPECT_EQ(jwt.parseFromString(JwtTextWithNonExistentKid), Status::Ok);
  // jwt has a kid, which did not match any of the keys in the jwks (even
  //   though the jwks does contain an appropriate key)
  EXPECT_EQ(verifyJwt(jwt, *correct_jwks_, 1), Status::JwksKidAlgMismatch);
}

} // namespace
} // namespace JwtVerify
} // namespace Envoy
