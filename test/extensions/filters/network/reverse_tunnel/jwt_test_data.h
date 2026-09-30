#pragma once

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ReverseTunnel {

// RS256 JWKS and matching pre-signed tokens for the handshake JWT tests. The JWKS is single-line so
// it also embeds in a YAML inline_string scalar.
const char kTestJwks[] =
    R"({"keys":[{"kty":"RSA","alg":"RS256","use":"sig","kid":"62a93512c9ee4c7f8067b5a216dade2763d32a47","n":"up97uqrF9MWOPaPkwSaBeuAPLOr9FKcaWGdVEGzQ4f3Zq5WKVZowx9TCBxmImNJ1qmUi13pB8otwM_l5lfY1AFBMxVbQCUXntLovhDaiSvYp4wGDjFzQiYA-pUq8h6MUZBnhleYrkU7XlCBwNVyN8qNMkpLA7KFZYz-486GnV2NIJJx_4BGa3HdKwQGxi2tjuQsQvao5W4xmSVaaEWopBwMy2QmlhSFQuPUpTaywTqUcUq_6SfAHhZ4IDa_FxEd2c2z8gFGtfst9cY3lRYf-c_ZdboY3mqN9Su3-j3z5r2SHWlhB_LNAjyWlBGsvbGPlTqDziYQwZN4aGsqVKQb9Vw","e":"AQAB"},{"kty":"RSA","n":"u1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0_IzW7yWR7QkrmBL7jTKEn5u-qKhbwKfBstIs-bMY2Zkp18gnTxKLxoS2tFczGkPLPgizskuemMghRniWaoLcyehkd3qqGElvW_VDL5AaWTg0nLVkjRo9z-40RQzuVaE8AkAFmxZzow3x-VJYKdjykkJ0iT9wCS0DRTXu269V264Vf_3jvredZiKRkgwlL9xNAwxXFg0x_XFw005UWVRIkdgcKWTjpBP2dPwVZ4WWC-9aGVd-Gyn1o0CLelf4rEjGoXbAAEgAqeGUxrcIlbjXfbcmw","e":"AQAB","alg":"RS256","use":"sig"}]})";

// A JWKS whose key does not sign kGoodToken; used to test key replacement on refresh.
const char kOtherJwks[] =
    R"({"keys":[{"kty":"RSA","n":"u1SU1LfVLPHCozMxH2Mo4lgOEePzNm0tRgeLezV6ffAt0gunVTLw7onLRnrq0_IzW7yWR7QkrmBL7jTKEn5u-qKhbwKfBstIs-bMY2Zkp18gnTxKLxoS2tFczGkPLPgizskuemMghRniWaoLcyehkd3qqGElvW_VDL5AaWTg0nLVkjRo9z-40RQzuVaE8AkAFmxZzow3x-VJYKdjykkJ0iT9wCS0DRTXu269V264Vf_3jvredZiKRkgwlL9xNAwxXFg0x_XFw005UWVRIkdgcKWTjpBP2dPwVZ4WWC-9aGVd-Gyn1o0CLelf4rEjGoXbAAEgAqeGUxrcIlbjXfbcmw","e":"AQAB","alg":"RS256","use":"sig"}]})";

const char kIssuer[] = "https://example.com";

const char kGoodToken[] = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
                          "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiY"
                          "XVkIjoiZXhhbXBsZV9zZXJ2aWNlIn0.cuui_Syud76B0tqvjESE8IZbX7vzG6xA-M"
                          "Daof1qEFNIoCFT_YQPkseLSUSR2Od3TJcNKk-dKjvUEL1JW3kGnyC1dBx4f3-Xxro"
                          "yL23UbR2eS8TuxO9ZcNCGkjfvH5O4mDb6cVkFHRDEolGhA7XwNiuVgkGJ5Wkrvshi"
                          "h6nqKXcPNaRx9lOaRWg2PkE6ySNoyju7rNfunXYtVxPuUIkl0KMq3WXWRb_cb8a_Z"
                          "EprqSZUzi_ZzzYzqBNVhIJujcNWij7JRra2sXXiSAfKjtxHQoxrX8n4V1ySWJ3_1T"
                          "H_cJcdfS_RKP7YgXRWC0L16PNF5K7iqRqmjKALNe83ZFnFIw";

const char kExpiredToken[] = "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
                             "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MTIwNTAwNTU4NywiY"
                             "XVkIjoiZXhhbXBsZV9zZXJ2aWNlIn0.izDa6aHNgbsbeRzucE0baXIP7SXOrgopYQ"
                             "ALLFAsKq_N0GvOyqpAZA9nwCAhqCkeKWcL-9gbQe3XJa0KN3FPa2NbW4ChenIjmf2"
                             "QYXOuOQaDu9QRTdHEY2Y4mRy6DiTZAsBHWGA71_cLX-rzTSO_8aC8eIqdHo898oJw"
                             "3E8ISKdryYjayb9X3wtF6KLgNomoD9_nqtOkliuLElD8grO0qHKI1xQurGZNaoeyi"
                             "V1AdwgX_5n3SmQTacVN0WcSgk6YJRZG6VE8PjxZP9bEameBmbSB0810giKRpdTU1-"
                             "RJtjq6aCSTD4CYXtW38T5uko4V-S4zifK3BXeituUTebkgoA";

const char kNonExpiringToken[] =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImlhdCI6MTUzMzE3NTk0Mn0.OSh-"
    "AcY9dCUibXiIZzPlTdEsYH8xP3QkCJDesO3LVu4ndgTrxDnNuR3I4_oV4tjtirmLZD3sx"
    "96wmLiIhOyqj3nipIdf_aQWcmET0XoRqGixOKse5FlHyU_VC1Jj9AlMvSz9zyCvKxMyP0"
    "CeA-bhI_Qs-I9vBPK8pd-EUOespUqWMQwNdtrOdXLcvF8EA5BV5G2qRGzCU0QJaW0Dpyj"
    "YF7ZCswRGorc2oMt5duXSp3-L1b9dDrnLwroxUrmQIZz9qvfwdDR-guyYSjKVQu5NJAyy"
    "sd8XKNzmHqJ2fYhRjc5s7l5nIWTDyBXSdPKQ8cBnfFKoxaRhmMBjdEn9RB7r6A";

} // namespace ReverseTunnel
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
