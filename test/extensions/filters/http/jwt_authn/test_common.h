#pragma once

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

// A good public key
const std::string PublicKey =
    "{\"keys\": [{"
    "  \"kty\": \"RSA\","
    "  \"n\": "
    "\"up97uqrF9MWOPaPkwSaBeuAPLOr9FKcaWGdVEGzQ4f3Zq5WKVZowx9TCBxmImNJ1q"
    "mUi13pB8otwM_l5lfY1AFBMxVbQCUXntLovhDaiSvYp4wGDjFzQiYA-pUq8h6MUZBnhleYrk"
    "U7XlCBwNVyN8qNMkpLA7KFZYz-486GnV2NIJJx_4BGa3HdKwQGxi2tjuQsQvao5W4xmSVaaE"
    "WopBwMy2QmlhSFQuPUpTaywTqUcUq_6SfAHhZ4IDa_FxEd2c2z8gFGtfst9cY3lRYf-c_Zdb"
    "oY3mqN9Su3-j3z5r2SHWlhB_LNAjyWlBGsvbGPlTqDziYQwZN4aGsqVKQb9Vw\","
    "  \"e\": \"AQAB\","
    "  \"alg\": \"RS256\","
    "  \"kid\": \"62a93512c9ee4c7f8067b5a216dade2763d32a47\""
    "},"
    "{"
    "  \"kty\": \"RSA\","
    "  \"n\": "
    "\"up97uqrF9MWOPaPkwSaBeuAPLOr9FKcaWGdVEGzQ4f3Zq5WKVZowx9TCBxmImNJ1q"
    "mUi13pB8otwM_l5lfY1AFBMxVbQCUXntLovhDaiSvYp4wGDjFzQiYA-pUq8h6MUZBnhleYrk"
    "U7XlCBwNVyN8qNMkpLA7KFZYz-486GnV2NIJJx_4BGa3HdKwQGxi2tjuQsQvao5W4xmSVaaE"
    "WopBwMy2QmlhSFQuPUpTaywTqUcUq_6SfAHhZ4IDa_FxEd2c2z8gFGtfst9cY3lRYf-c_Zdb"
    "oY3mqN9Su3-j3z5r2SHWlhB_LNAjyWlBGsvbGPlTqDziYQwZN4aGsqVKQb9Vw\","
    "  \"e\": \"AQAB\","
    "  \"alg\": \"RS256\","
    "  \"kid\": \"b3319a147514df7ee5e4bcdee51350cc890cc89e\""
    "}]}";

// A good config.
const char ExampleConfig[] = R"(
rules:
  - issuer: https://example.com
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
)";

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
