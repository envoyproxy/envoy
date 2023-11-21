#pragma once

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

// RS256 private key
// -----BEGIN PRIVATE KEY-----
//    MIIEvAIBADANBgkqhkiG9w0BAQEFAASCBKYwggSiAgEAAoIBAQC6n3u6qsX0xY49
//    o+TBJoF64A8s6v0UpxpYZ1UQbNDh/dmrlYpVmjDH1MIHGYiY0nWqZSLXekHyi3Az
//    +XmV9jUAUEzFVtAJRee0ui+ENqJK9injAYOMXNCJgD6lSryHoxRkGeGV5iuRTteU
//    IHA1XI3yo0ySksDsoVljP7jzoadXY0gknH/gEZrcd0rBAbGLa2O5CxC9qjlbjGZJ
//    VpoRaikHAzLZCaWFIVC49SlNrLBOpRxSr/pJ8AeFnggNr8XER3ZzbPyAUa1+y31x
//    jeVFh/5z9l1uhjeao31K7f6PfPmvZIdaWEH8s0CPJaUEay9sY+VOoPOJhDBk3hoa
//    ypUpBv1XAgMBAAECggEAc5HaJJIm/trsqD17pyV6X6arnyxyx7xn80Eii4ZnoNv8
//    VWbJARP4i3e1JIJqdgE3PutctUYP2u0A8h7XbcfHsMcJk9ecA3IX+HKohF71CCkD
//    bYH9fgnoVo5lvSTYNcMHGKpyacrdRiImHKQt+M21VgJMpCRfdurAmVbX6YA9Sj6w
//    SBFrZbWkBHiHg7w++xKr+VeTHW/8fXI5bvSPAm/XB6dDKAcSXYiJJJhIoaVR9cHn
//    1ePRDLpEwfDpBHeepd/S3qR37mIbHmo8SVytDY2xTUaIoaRfXRWGMYSyxl0y4RsZ
//    Vo6Tp9Tj2fyohvB/S+lE34zhxnsHToK2JZvPeoyHCQKBgQDyEcjaUZiPdx7K63CT
//    d57QNYC6DTjtKWnfO2q/vAVyAPwS30NcVuXj3/1yc0L+eExpctn8tcLfvDi1xZPY
//    dW2L3SZKgRJXL+JHTCEkP8To/qNLhBqitcKYwp0gtpoZbUjZdZwn18QJx7Mw/nFC
//    lJhSYRl+FjVolY3qBaS6eD7imwKBgQDFXNmeAV5FFF0FqGRsLYl0hhXTR6Hi/hKQ
//    OyRALBW9LUKbsazwWEFGRlqbEWd1OcOF5SSV4d3u7wLQRTDeNELXUFvivok12GR3
//    gNl9nDJ5KKYGFmqxM0pzfbT5m3Lsrr2FTIq8gM9GBpQAOmzQIkEu62yELtt2rRf0
//    1pTh+UbN9QKBgF88kAEUySjofLzpFElwbpML+bE5MoRcHsMs5Tq6BopryMDEBgR2
//    S8vzfAtjPaBQQ//Yp9q8yAauTsF1Ek2/JXI5d68oSMb0l9nlIcTZMedZB3XWa4RI
//    bl8bciZEsSv/ywGDPASQ5xfR8bX85SKEw8jlWto4cprK/CJuRfj3BgaxAoGAAmQf
//    ltR5aejXP6xMmyrqEWlWdlrV0UQ2wVyWEdj24nXb6rr6V2caU1mi22IYmMj8X3Dp
//    Qo+b+rsWk6Ni9i436RfmJRcd3nMitHfxKp5r1h/x8vzuifsPGdsaCDQj7k4nqafF
//    vobo+/Y0cNREYTkpBQKBLBDNQ+DQ+3xmDV7RxskCgYBCo6u2b/DZWFLoq3VpAm8u
//    1ZgL8qxY/bbyA02IKF84QPFczDM5wiLjDGbGnOcIYYMvTHf1LJU4FozzYkB0GicX
//    Y0tBQIHaaLWbPk1RZdPfR9kAp16iwk8H+V4UVjLfsTP7ocEfNCzZztmds83h8mTL
//    DSwE5aY76Cs8XLcF/GNJRQ==
// -----END PRIVATE KEY-----

// A good public key
const char PublicKey[] = R"(
{
  "keys": [
    {
      "kty": "RSA",
      "alg": "RS256",
      "use": "sig",
      "kid": "62a93512c9ee4c7f8067b5a216dade2763d32a47",
      "n": "up97uqrF9MWOPaPkwSaBeuAPLOr9FKcaWGdVEGzQ4f3Zq5WKVZowx9TCBxmImNJ1qmUi13pB8otwM_l5lfY1AFBMxVbQCUXntLovhDaiSvYp4wGDjFzQiYA-pUq8h6MUZBnhleYrkU7XlCBwNVyN8qNMkpLA7KFZYz-486GnV2NIJJx_4BGa3HdKwQGxi2tjuQsQvao5W4xmSVaaEWopBwMy2QmlhSFQuPUpTaywTqUcUq_6SfAHhZ4IDa_FxEd2c2z8gFGtfst9cY3lRYf-c_ZdboY3mqN9Su3-j3z5r2SHWlhB_LNAjyWlBGsvbGPlTqDziYQwZN4aGsqVKQb9Vw",
      "e": "AQAB"
    },
    {
      "kty": "RSA",
      "alg": "RS256",
      "use": "sig",
      "kid": "b3319a147514df7ee5e4bcdee51350cc890cc89e",
      "n": "up97uqrF9MWOPaPkwSaBeuAPLOr9FKcaWGdVEGzQ4f3Zq5WKVZowx9TCBxmImNJ1qmUi13pB8otwM_l5lfY1AFBMxVbQCUXntLovhDaiSvYp4wGDjFzQiYA-pUq8h6MUZBnhleYrkU7XlCBwNVyN8qNMkpLA7KFZYz-486GnV2NIJJx_4BGa3HdKwQGxi2tjuQsQvao5W4xmSVaaEWopBwMy2QmlhSFQuPUpTaywTqUcUq_6SfAHhZ4IDa_FxEd2c2z8gFGtfst9cY3lRYf-c_ZdboY3mqN9Su3-j3z5r2SHWlhB_LNAjyWlBGsvbGPlTqDziYQwZN4aGsqVKQb9Vw",
      "e": "AQAB"
    },
    {
      "kty": "RSA",
      "n": "u1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0_IzW7yWR7QkrmBL7jTKEn5u-qKhbwKfBstIs-bMY2Zkp18gnTxKLxoS2tFczGkPLPgizskuemMghRniWaoLcyehkd3qqGElvW_VDL5AaWTg0nLVkjRo9z-40RQzuVaE8AkAFmxZzow3x-VJYKdjykkJ0iT9wCS0DRTXu269V264Vf_3jvredZiKRkgwlL9xNAwxXFg0x_XFw005UWVRIkdgcKWTjpBP2dPwVZ4WWC-9aGVd-Gyn1o0CLelf4rEjGoXbAAEgAqeGUxrcIlbjXfbcmw",
      "e": "AQAB",
      "alg": "RS256",
      "use": "sig"
    },
  ]
}
)";

// A good config.
const char ExampleConfig[] = R"(
providers:
  example_provider:
    issuer: https://example.com
    audiences:
    - example_service
    - http://example_service1
    - https://example_service2/
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
        timeout:
          seconds: 5
      cache_duration:
        seconds: 600
    forward_payload_header: sec-istio-auth-userinfo
    claim_to_headers:
    - header_name: "x-jwt-claim-sub"
      claim_name: "sub"
    - header_name: "x-jwt-claim-nested"
      claim_name: "nested.key-1"
    - header_name: "x-jwt-claim-nested-wrong"
      claim_name: "nested.wrong.claim"
    - header_name: "x-jwt-unsupported-type-claim"
      claim_name: "nested.nested-2.key-5[0]"
    - header_name: "x-jwt-bool-claim"
      claim_name: "nested.nested-2.key-3"
    - header_name: "x-jwt-int-claim"
      claim_name: "nested.nested-2.key-4"
    - header_name: "x-jwt-claim-object-key"
      claim_name: "nested.nested-2.key-5"
rules:
- match:
    path: "/"
  requires:
    provider_name: "example_provider"
bypass_cors_preflight: true
)";

// Config with claim_to_headers and clear_route_cache.
const char ClaimToHeadersConfig[] = R"(
providers:
  example_provider:
    issuer: https://example.com
    audiences:
    - example_service
    - http://example_service1
    - https://example_service2/
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
        timeout:
          seconds: 5
      cache_duration:
        seconds: 600
    claim_to_headers:
    - header_name: "x-jwt-claim-nested"
      claim_name: "nested.key-1"
    clear_route_cache: true
rules:
- match:
    path: "/"
  requires:
    provider_name: "example_provider"
bypass_cors_preflight: true
)";

const char ExampleConfigWithRegEx[] = R"(
providers:
  example_provider:
    issuer: https://example.com
    audiences:
    - example_service
    - http://example_service1
    - https://example_service2/
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
        timeout:
          seconds: 5
      cache_duration:
        seconds: 600
    forward_payload_header: sec-istio-auth-userinfo
rules:
- match:
    safe_regex:
      regex: "/somethig/.*"
  requires:
    provider_name: "example_provider"
- match:
    path: "/"
  requires:
    provider_name: "example_provider"
bypass_cors_preflight: true
)";

// The name of provider for above config.
const char ProviderName[] = "example_provider";

// Payload:
// {"iss":"https://example.com","sub":"test@example.com","aud":"example_service","exp":2001001001}
const char GoodToken[] = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
                         "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiY"
                         "XVkIjoiZXhhbXBsZV9zZXJ2aWNlIn0.cuui_Syud76B0tqvjESE8IZbX7vzG6xA-M"
                         "Daof1qEFNIoCFT_YQPkseLSUSR2Od3TJcNKk-dKjvUEL1JW3kGnyC1dBx4f3-Xxro"
                         "yL23UbR2eS8TuxO9ZcNCGkjfvH5O4mDb6cVkFHRDEolGhA7XwNiuVgkGJ5Wkrvshi"
                         "h6nqKXcPNaRx9lOaRWg2PkE6ySNoyju7rNfunXYtVxPuUIkl0KMq3WXWRb_cb8a_Z"
                         "EprqSZUzi_ZzzYzqBNVhIJujcNWij7JRra2sXXiSAfKjtxHQoxrX8n4V1ySWJ3_1T"
                         "H_cJcdfS_RKP7YgXRWC0L16PNF5K7iqRqmjKALNe83ZFnFIw";

// Payload:
// {
//   "iss": "https://example.com",
//   "sub": "test@example.com",
//   "exp": 2001001001,
//   "aud": "example_service",
//   "scope": "read write",
//   "test_string": "test_value",
//   "test_num": 1337
// }
const char GoodTokenWithSpaces[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MjAwMTAwMTAwMS"
    "wiYXVkIjoiZXhhbXBsZV9zZXJ2aWNlIiwic2NvcGUiOiJyZWFkIHdyaXRlIiwidGVzdF9zdHJpbmciOiJ0ZXN0X3ZhbHVl"
    "IiwidGVzdF9udW0iOjEzMzd9.cKTwWSJgS0TZ3Ajc9QrAA50Me7j1zVv9YzDT_"
    "2UE5jlCs5vWkdWjUb2r7MYaqximXj3affDZdDsUxMaqqR7lWT2EbxOoEceBkCMmakgSs8tjZ210w0YTU0OyhrrxsyxUpsp"
    "PeRzPIHQTUdN7zU_KkMcUU1yDSlnJxqlYXyTL9E-DhTnLwoOdgFGiQs-md_QJfdOFgXQqU71EZ-"
    "Ofxen8EFl10wbzHubMHGLJqVfFzK-iuVr2P0OZ0ymWvPGwQdlVMojHx3P0Yb8MRbhdW04hCJq-_"
    "fTE1RNb6ja1JBFQbyGcQTtWVSdkHZ_C8syd8s-aK4C8_VhwNEDviOVrHPbztw";

// Payload:
// {"iss":"https://example.com","sub":"test@example.com","exp":null}
const char NonExpiringToken[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImlhdCI6MTUzMzE3NTk0Mn0.OSh-"
    "AcY9dCUibXiIZzPlTdEsYH8xP3QkCJDesO3LVu4ndgTrxDnNuR3I4_oV4tjtirmLZD3sx"
    "96wmLiIhOyqj3nipIdf_aQWcmET0XoRqGixOKse5FlHyU_VC1Jj9AlMvSz9zyCvKxMyP0"
    "CeA-bhI_Qs-I9vBPK8pd-EUOespUqWMQwNdtrOdXLcvF8EA5BV5G2qRGzCU0QJaW0Dpyj"
    "YF7ZCswRGorc2oMt5duXSp3-L1b9dDrnLwroxUrmQIZz9qvfwdDR-guyYSjKVQu5NJAyy"
    "sd8XKNzmHqJ2fYhRjc5s7l5nIWTDyBXSdPKQ8cBnfFKoxaRhmMBjdEn9RB7r6A";

// An expired token
// {"iss":"https://example.com","sub":"test@example.com","aud":"example_service","exp":1205005587}
const char ExpiredToken[] = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
                            "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MTIwNTAwNTU4NywiY"
                            "XVkIjoiZXhhbXBsZV9zZXJ2aWNlIn0.izDa6aHNgbsbeRzucE0baXIP7SXOrgopYQ"
                            "ALLFAsKq_N0GvOyqpAZA9nwCAhqCkeKWcL-9gbQe3XJa0KN3FPa2NbW4ChenIjmf2"
                            "QYXOuOQaDu9QRTdHEY2Y4mRy6DiTZAsBHWGA71_cLX-rzTSO_8aC8eIqdHo898oJw"
                            "3E8ISKdryYjayb9X3wtF6KLgNomoD9_nqtOkliuLElD8grO0qHKI1xQurGZNaoeyi"
                            "V1AdwgX_5n3SmQTacVN0WcSgk6YJRZG6VE8PjxZP9bEameBmbSB0810giKRpdTU1-"
                            "RJtjq6aCSTD4CYXtW38T5uko4V-S4zifK3BXeituUTebkgoA";

// An NotYetValid token
// {"iss":"https://example.com","sub":"test@example.com","aud":"example_service","nbf":9223372036854775807}
const char NotYetValidToken[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImF1"
    "ZCI6ImV4YW1wbGVfc2VydmljZSIsIm5iZiI6OTIyMzM3MjAzNjg1NDc3NTgwN30K"
    ".izDa6aHNgbsbeRzucE0baXIP7SXOrgopYQ"
    "ALLFAsKq_N0GvOyqpAZA9nwCAhqCkeKWcL-9gbQe3XJa0KN3FPa2NbW4ChenIjmf2"
    "QYXOuOQaDu9QRTdHEY2Y4mRy6DiTZAsBHWGA71_cLX-rzTSO_8aC8eIqdHo898oJw"
    "3E8ISKdryYjayb9X3wtF6KLgNomoD9_nqtOkliuLElD8grO0qHKI1xQurGZNaoeyi"
    "V1AdwgX_5n3SmQTacVN0WcSgk6YJRZG6VE8PjxZP9bEameBmbSB0810giKRpdTU1-"
    "RJtjq6aCSTD4CYXtW38T5uko4V-S4zifK3BXeituUTebkgoA";

// A token with "aud" as invalid_service
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","aud":"invalid_service","exp":2001001001}
const char InvalidAudToken[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiY"
    "XVkIjoiaW52YWxpZF9zZXJ2aWNlIn0.B9HuVXpRDVYIvApfNQmE_l5fEMPEiPdi-s"
    "dKbTione8I_UsnYHccKZVegaF6f2uyWhAvaTPgaMosyDlJD6skadEcmZD0V4TzsYK"
    "v7eP5FQga26hZ1Kra7n9hAq4oFfH0J8aZLOvDV3tAgCNRXlh9h7QiBPeDNQlwztqE"
    "csyp1lHI3jdUhsn3InIn-vathdx4PWQWLVb-74vwsP-END-MGlOfu_TY5OZUeY-GB"
    "E4Wr06aOSU2XQjuNr6y2WJGMYFsKKWfF01kHSuyc9hjnq5UI19WrOM8s7LFP4w2iK"
    "WFIPUGmPy3aM0TiF2oFOuuMxdPR3HNdSG7EWWRwoXv7n__jA";

// A token with non exist kid
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","aud":"example_service","exp":2001001001}
const char NonExistKidToken[] =
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

// {"iss":"https://other.com","sub":"test@other.com","aud":"other_service","exp":2001001001}
const char OtherGoodToken[] =
    "eyJ0eXAiOiJKV1QiLCJhbGciOiJSUzI1NiJ9."
    "eyJpc3MiOiJodHRwczovL290aGVyLmNvbSIsInN1YiI6InRlc3RAb3RoZXIuY29tIiwiYXVkIjoib3RoZXJfc2VydmljZS"
    "IsImV4cCI6MjAwMTAwMTAwMX0.R0GR2rnRTg_gWzDvuO-BXVMmw3-vyBspV_kUQ4zvIdO-_"
    "1icaWzbioPTPEyoViWuErNYxaZ5YFBoD6Zk_hIe1YWoSJr9QRwxWA4CWcasJdBXPq2mMETt8VjAiXE_"
    "aIrJOLIlP786GLjVgTsnvhaDUJyU7xUdoi9HRjEBYcdjNPvxJutoby8MypAkwdGxjl4H4Z01gomgWyUDRRy47OKI_"
    "buwXk5M6d-"
    "drRvLcvlT5gB4adOIOlmhm8xtXgYpvqrXfmMJCHbP9no7JATFaTEAkmA3OOxDsaOju4BFgMtRZtDM8p12QQG0rFl_FE-"
    "2FqYX9qA4q41HJ4vxTSxgObeLGA";

//{
//  "iss": "https://example.com",
//  "sub": "test@example.com",
//  "aud": "example_service",
//  "exp": 2001001001,
//  "nested": {
//    "key-1": "value1",
//    "nested-2": {
//      "key-2": "value2",
//      "key-3": true,
//      "key-4": 9999,
//      "key-5": ["str1", "str2"]
//  }
// }
//}
const char NestedGoodToken[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9."
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImF1ZCI6ImV4YW1wbGVfc2"
    "VydmljZSIsImV4cCI6MjAwMTAwMTAwMSwibmVzdGVkIjp7ImtleS0xIjoidmFsdWUxIiwibmVzdGVkLTIiOnsia2V5LTIi"
    "OiJ2YWx1ZTIiLCJrZXktMyI6dHJ1ZSwia2V5LTQiOjk5OTksImtleS01IjpbInN0cjEiLCJzdHIyIl19fX0."
    "uCXNQUSualacToI0gyXz5NStad2aNBfZHiLB5mTddy-uV8Dxsa8U81NwDHGlvNksmMTodBwIeEYJ0ISkl03kKN_"
    "VepiEpNNWJNpgOcdTqAE9-aGip-kGWZAavJS6r3fystUGIjXyI9EKNEqeUFCihpiII-"
    "MTbcqdaSzwdbJPeGQzHyensPG6BfDjdv39b_gdO_eH1azaVwi4HnChoJcsGrBjsH6-IyJVR6Ux_"
    "43fo3Wbs0SB82hLpiWPsucO7l4CyII5d5jPQbAM9ajcvAmh7FprIsf35acOT2bQ8dmrSD9KSjsYomkF_OAci-"
    "osyRzYOgkGHIDGDyjj87xaPPuzIw";

// Expected base64 payload value.
const char ExpectedPayloadValue[] = "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcG"
                                    "xlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiYXVkIjoiZXhhbXBsZV9zZXJ2"
                                    "aWNlIn0";

const char ExpectedPayloadValueWithPadding[] =
    "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcG"
    "xlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiYXVkIjoiZXhhbXBsZV9zZXJ2"
    "aWNlIn0=";

// Base64 decoded Payload JSON
const char ExpectedPayloadJSON[] = R"(
{
  "iss":"https://example.com",
  "sub":"test@example.com",
  "exp":2001001001,
  "aud":"example_service"
}
)";

// Base64 decoded Payload with space-delimited claims JSON
const char ExpectedPayloadJSONWithSpaces[] = R"(
{
  "iss":"https://example.com",
  "sub":"test@example.com",
  "exp":2001001001,
  "aud":"example_service",
  "scope":["read","write"],
  "test_string":["test_value"],
  "test_num":1337
}
)";

const char ExpectedHeaderJSON[] = R"(
{
  "alg": "RS256",
  "typ": "JWT"
}
)";

const char ExpectedPayloadAndHeaderJSON[] = R"(
{
  "my_payload":{
    "iss":"https://example.com",
    "exp":2001001001,
    "sub":"test@example.com",
    "aud":"example_service"
  },
  "my_header":{
    "typ":"JWT",
    "alg":"RS256"
  }
}
)";

// Token copied from https://github.com/google/jwt_verify_lib/blob/master/src/verify_jwk_ec_test.cc
// Use jwt.io to modify payload as:
// {
//  "iss11": "628645741881-noabiu23f5a8m8ovd8ucv698lj78vv0l@developer.gserviceaccount.com",
//  "sub": "628645741881-noabiu23f5a8m8ovd8ucv698lj78vv0l@developer.gserviceaccount.com",
//  "aud": "example_service"
// }
const char ES256WithoutIssToken[] =
    "eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6ImFiYyJ9."
    "eyJpc3MxMSI6IjYyODY0NTc0MTg4MS1ub2FiaXUyM2Y1YThtOG92ZDh1Y3Y2OThsajc4dnYwbEBkZXZlbG9wZXIuZ3Nlcn"
    "ZpY2VhY2NvdW50LmNvbSIsInN1YiI6IjYyODY0NTc0MTg4MS1ub2FiaXUyM2Y1YThtOG92ZDh1Y3Y2OThsajc4dnYwbEBk"
    "ZXZlbG9wZXIuZ3NlcnZpY2VhY2NvdW50LmNvbSIsImF1ZCI6ImV4YW1wbGVfc2VydmljZSJ9.f-_"
    "NAdznQK9o93AZTqNawu1yal1igNLgYvuj0JzW9SVmJJQuBT_12wJi9XQzZLFwSFn6D3f7bPWSZQlScTFSMg";

// Public key for above ES256 tokens.
const char ES256PublicKey[] = R"(
{
  "keys": [
    {
      "kty": "EC",
      "crv": "P-256",
      "alg": "ES256",
      "kid": "abc",
      "x": "EB54wykhS7YJFD6RYJNnwbWEz3cI7CF5bCDTXlrwI5k",
      "y": "92bCBTvMFQ8lKbS2MbgjT3YfmYo6HnPEE2tsAqWUJw8"
    }
  ]
}
)";

// Config with requires_all requirement
const char RequiresAllConfig[] = R"(
providers:
  example_provider:
    issuer: https://example.com
    audiences:
    - example_service
    - http://example_service1
    - https://example_service2/
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
    from_params: ["jwt_a"]
    forward_payload_header: example-auth-userinfo
  other_provider:
    issuer: https://other.com
    audiences:
    - other_service
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
    from_params: ["jwt_b"]
    forward_payload_header: other-auth-userinfo
rules:
- match:
    path: "/requires-all"
  requires:
    requires_all:
      requirements:
      - provider_name: "example_provider"
      - provider_name: "other_provider"
)";
// Config with requires_any requirement
const char RequiresAnyConfig[] = R"(
providers:
  example_provider:
    issuer: https://example.com
    audiences:
    - example_service
    - http://example_service1
    - https://example_service2/
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
    from_headers:
    - name: a
      value_prefix: "Bearer "
    - name: b
      value_prefix: "Bearer "
    forward_payload_header: example-auth-userinfo
  other_provider:
    issuer: https://other.com
    audiences:
    - other_service
    remote_jwks:
      http_uri:
        uri: https://pubkey_server/pubkey_path
        cluster: pubkey_cluster
    from_headers:
    - name: a
      value_prefix: "Bearer "
    - name: b
      value_prefix: "Bearer "
    forward_payload_header: other-auth-userinfo
rules:
- match:
    path: "/requires-any"
  requires:
    requires_any:
      requirements:
      - provider_name: "example_provider"
      - provider_name: "other_provider"
)";

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
