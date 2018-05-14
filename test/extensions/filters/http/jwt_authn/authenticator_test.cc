#include "common/http/message_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/authenticator.h"

#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::google::jwt_verify::Status;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

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

// A good JSON config.
const char ExampleConfig[] = R"(
{
   "rules": [
      {
         "issuer": "https://example.com",
         "audiences": [
            "example_service",
            "http://example_service1",
            "https://example_service2/"
          ],
          "remote_jwks": {
            "http_uri": {
              "uri": "https://pubkey_server/pubkey_path",
              "cluster": "pubkey_cluster"
            },
            "cache_duration": {
              "seconds": 600
            }
         },
         "forward_payload_header": "sec-istio-auth-userinfo"
      }
   ]
}
)";

// A JSON config without forward_payload_header configured.
const char ExampleConfigWithoutForwardPayloadHeader[] = R"(
{
   "rules": [
      {
         "issuer": "https://example.com",
         "audiences": [
            "example_service",
            "http://example_service1",
            "https://example_service2/"
          ],
          "remote_jwks": {
            "http_uri": {
              "uri": "https://pubkey_server/pubkey_path",
              "cluster": "pubkey_cluster"
            },
            "cache_duration": {
              "seconds": 600
            }
         },
      }
   ]
}
)";

// An example JSON config with a good JWT config and allow_missing_or_failed
// option enabled
const char ExampleConfigWithJwtAndAllowMissingOrFailed[] = R"(
{
   "rules": [
      {
         "issuer": "https://example.com",
         "audiences": [
            "example_service",
            "http://example_service1",
            "https://example_service2/"
          ],
          "remote_jwks": {
            "http_uri": {
              "uri": "https://pubkey_server/pubkey_path",
              "cluster": "pubkey_cluster"
            },
            "cache_duration": {
              "seconds": 600
            }
         }
      }
   ],
  "allow_missing_or_failed": true
}
)";

// A JSON config for "other_issuer"
const char OtherIssuerConfig[] = R"(
{
   "rules": [
      {
         "issuer": "other_issuer"
      }
   ]
}
)";

// A config with bypass
const char BypassConfig[] = R"(
{
  "bypass": [
  ]
}
)";

// expired token
// {"iss":"https://example.com","sub":"test@example.com","aud":"example_service","exp":1205005587}
const std::string ExpiredToken =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MTIwNTAwNTU4NywiY"
    "XVkIjoiZXhhbXBsZV9zZXJ2aWNlIn0.izDa6aHNgbsbeRzucE0baXIP7SXOrgopYQ"
    "ALLFAsKq_N0GvOyqpAZA9nwCAhqCkeKWcL-9gbQe3XJa0KN3FPa2NbW4ChenIjmf2"
    "QYXOuOQaDu9QRTdHEY2Y4mRy6DiTZAsBHWGA71_cLX-rzTSO_8aC8eIqdHo898oJw"
    "3E8ISKdryYjayb9X3wtF6KLgNomoD9_nqtOkliuLElD8grO0qHKI1xQurGZNaoeyi"
    "V1AdwgX_5n3SmQTacVN0WcSgk6YJRZG6VE8PjxZP9bEameBmbSB0810giKRpdTU1-"
    "RJtjq6aCSTD4CYXtW38T5uko4V-S4zifK3BXeituUTebkgoA";

// A token with aud as invalid_service
// Payload:
// {"iss":"https://example.com","sub":"test@example.com","aud":"invalid_service","exp":2001001001}
const std::string InvalidAudToken =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiY"
    "XVkIjoiaW52YWxpZF9zZXJ2aWNlIn0.B9HuVXpRDVYIvApfNQmE_l5fEMPEiPdi-s"
    "dKbTione8I_UsnYHccKZVegaF6f2uyWhAvaTPgaMosyDlJD6skadEcmZD0V4TzsYK"
    "v7eP5FQga26hZ1Kra7n9hAq4oFfH0J8aZLOvDV3tAgCNRXlh9h7QiBPeDNQlwztqE"
    "csyp1lHI3jdUhsn3InIn-vathdx4PWQWLVb-74vwsP-END-MGlOfu_TY5OZUeY-GB"
    "E4Wr06aOSU2XQjuNr6y2WJGMYFsKKWfF01kHSuyc9hjnq5UI19WrOM8s7LFP4w2iK"
    "WFIPUGmPy3aM0TiF2oFOuuMxdPR3HNdSG7EWWRwoXv7n__jA";

// Payload:
// {"iss":"https://example.com","sub":"test@example.com","aud":"example_service","exp":2001001001}
const std::string GoodToken =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiY"
    "XVkIjoiZXhhbXBsZV9zZXJ2aWNlIn0.cuui_Syud76B0tqvjESE8IZbX7vzG6xA-M"
    "Daof1qEFNIoCFT_YQPkseLSUSR2Od3TJcNKk-dKjvUEL1JW3kGnyC1dBx4f3-Xxro"
    "yL23UbR2eS8TuxO9ZcNCGkjfvH5O4mDb6cVkFHRDEolGhA7XwNiuVgkGJ5Wkrvshi"
    "h6nqKXcPNaRx9lOaRWg2PkE6ySNoyju7rNfunXYtVxPuUIkl0KMq3WXWRb_cb8a_Z"
    "EprqSZUzi_ZzzYzqBNVhIJujcNWij7JRra2sXXiSAfKjtxHQoxrX8n4V1ySWJ3_1T"
    "H_cJcdfS_RKP7YgXRWC0L16PNF5K7iqRqmjKALNe83ZFnFIw";

// Payload:
// {"iss":"https://example.com","sub":"test@example.com","aud":"http://example_service/","exp":2001001001}
const std::string GoodTokenAudHasProtocolScheme =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiY"
    "XVkIjoiaHR0cDovL2V4YW1wbGVfc2VydmljZS8ifQ.gHqO8m3hUZZ8m7EajMQy8vB"
    "RL5o3njwU5Pg2NxU4z3AwUP6P_7MoB_ChiByjg_LQ92GjHXbHn1gAQHVOn0hERVwm"
    "VYGmNsZHm4k5pmD6orPcYV1i3DdLqqxEVyw2R1XD8bC9zK7Tc8mKTRIJYC4T1QSo8"
    "mKTzZ8M-EwAuDYa0CsWGhIfA4o3xChXKPLM2hxA4uM1A6s4AQ4ipNQ5FTgLDabgsC"
    "EpfDR3lAXSaug1NE22zX_tm0d9JnC5ZrIk3kwmPJPrnAS2_9RKTQW2e2skpAT8dUV"
    "T5aSpQxJmWIkyp4PKWmH6h4H2INS7hWyASZdX4oW-R0PMy3FAd8D6Y8740A";

// Payload:
// {"iss":"https://example.com","sub":"test@example.com","aud":"https://example_service1/","exp":2001001001}
const std::string GoodTokenAudService1 =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiY"
    "XVkIjoiaHR0cHM6Ly9leGFtcGxlX3NlcnZpY2UxLyJ9.JJq_-fzbNWykI2npW13hJ"
    "F_2_IK9JAlodt_T_kO_kSCb7ngAJvmbDhnIUKp7PX-UCEx_6sehNnLZzZeazGeDgw"
    "xcjI4zM7E1bzus_sY_Kl7MSYBx7UyW0rgbEvjJOg681Uwn8MkQh9wfQ-SuzPfe07Y"
    "O4bFMuNBiZsxS0j3_agJrbmpEPycNBSIZ0ez3aQpnDyUgZ1ZGBoVOgzXUJDXptb71"
    "nzvwse8DINafa5kOhBmQcrIADiOyTVC1IqcOvaftVcS4MTkTeCyzfsqcNQ-VeNPKY"
    "3e6wTe9brxbii-IPZFNY-1osQNnfCtYpEDjfvMjwHTielF-b55xq_tUwuqaaQ";

// Payload:
// {"iss":"https://example.com","sub":"test@example.com","aud":"http://example_service2","exp":2001001001}
const std::string GoodTokenAudService2 =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJodHRwczovL2V4YW1wbGUu"
    "Y29tIiwic3ViIjoidGVzdEBleGFtcGxlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiY"
    "XVkIjoiaHR0cDovL2V4YW1wbGVfc2VydmljZTIifQ.XFPQHdA5A2rpoQgMMcCBRcW"
    "t8QrwVJAhdTgNqBjga_ebnoWZdzj9C6t-8mYYoCQ6t7bulLFbPzO8iJREo7zxN7Rn"
    "F0-15ur16LV7AYeDnH0istAiti9uy3POW3telcN374hbBVdA6sBafGqzeQ8cDpb4o"
    "0T_BIy6-kaz3ne4-UEdl8kLrR7UaA_LYrdXGomYKqwH3Q4q4mnV7mpE0YUm98AyI6"
    "Thwt7f3DTmHOMBeO_3xrLOOZgNtuXipqupkp9sb-DcCRdSokoFpGSTibvV_8RwkQo"
    "W2fdqw_ZD7WOe4sTcK27Uma9exclisHVxzJJbQOW82WdPQGicYaR_EajYzA";

} // namespace

class MockAuthenticatorCallbacks : public Authenticator::Callbacks {
public:
  MOCK_METHOD1(onComplete, void(const Status& status));
};

class AuthenticatorTest : public ::testing::Test {
public:
  void SetUp() { SetupConfig(ExampleConfig); }

  void SetupConfig(const std::string& json_str) {
    MessageUtil::loadFromJson(json_str, config_);
    store_factory_ = ::std::make_shared<DataStoreFactory>(config_, mock_factory_ctx_);
    auth_ = Authenticator::create(store_factory_);
  }

  JwtAuthentication config_;
  std::unique_ptr<Authenticator> auth_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  DataStoreFactorySharedPtr store_factory_;
  MockAuthenticatorCallbacks mock_cb_;
};

// A mock HTTP upstream with response body.
class MockUpstream {
public:
  MockUpstream(Upstream::MockClusterManager& mock_cm, const std::string& response_body)
      : request_(&mock_cm.async_client_), response_body_(response_body) {
    ON_CALL(mock_cm.async_client_, send_(_, _, _))
        .WillByDefault(Invoke([this](Http::MessagePtr&, Http::AsyncClient::Callbacks& cb,
                                     const absl::optional<std::chrono::milliseconds>&)
                                  -> Http::AsyncClient::Request* {
          Http::MessagePtr response_message(new Http::ResponseMessageImpl(
              Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
          response_message->body().reset(new Buffer::OwnedImpl(response_body_));
          cb.onSuccess(std::move(response_message));
          called_count_++;
          return &request_;
        }));
  }

  int called_count() const { return called_count_; }

private:
  Http::MockAsyncClientRequest request_;
  std::string response_body_;
  int called_count_{};
};

TEST_F(AuthenticatorTest, TestOkJWTandCache) {
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  // Test OK pubkey and its cache
  for (int i = 0; i < 10; i++) {
    auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodToken}};

    MockAuthenticatorCallbacks mock_cb;
    EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
      ASSERT_EQ(status, Status::Ok);
    }));

    auth_->verify(headers, &mock_cb);

    EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"),
              "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcG"
              "xlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiYXVkIjoiZXhhbXBsZV9zZXJ2"
              "aWNlIn0");
    // Verify the token is removed.
    EXPECT_FALSE(headers.Authorization());
  }

  EXPECT_EQ(mock_pubkey.called_count(), 1);
}

TEST_F(AuthenticatorTest, TestOkJWTPubkeyNoAlg) {
  // Test OK pubkey with no "alg" claim.
  std::string alg_claim = "  \"alg\": \"RS256\",";
  std::string pubkey_no_alg = PublicKey;
  std::size_t alg_pos = pubkey_no_alg.find(alg_claim);
  while (alg_pos != std::string::npos) {
    pubkey_no_alg.erase(alg_pos, alg_claim.length());
    alg_pos = pubkey_no_alg.find(alg_claim);
  }
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, pubkey_no_alg);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodToken}};

  MockAuthenticatorCallbacks mock_cb;
  EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  auth_->verify(headers, &mock_cb);

  EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"),
            "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcG"
            "xlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiYXVkIjoiZXhhbXBsZV9zZXJ2"
            "aWNlIn0");
  // Verify the token is removed.
  EXPECT_FALSE(headers.Authorization());

  EXPECT_EQ(mock_pubkey.called_count(), 1);
}

TEST_F(AuthenticatorTest, TestOkJWTPubkeyNoKid) {
  // Test OK pubkey with no "kid" claim.
  std::string kid_claim1 = ",  \"kid\": \"62a93512c9ee4c7f8067b5a216dade2763d32a47\"";
  std::string kid_claim2 = ",  \"kid\": \"b3319a147514df7ee5e4bcdee51350cc890cc89e\"";
  std::string pubkey_no_kid = PublicKey;
  std::size_t kid_pos = pubkey_no_kid.find(kid_claim1);
  pubkey_no_kid.erase(kid_pos, kid_claim1.length());
  kid_pos = pubkey_no_kid.find(kid_claim2);
  pubkey_no_kid.erase(kid_pos, kid_claim2.length());

  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, pubkey_no_kid);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodToken}};

  MockAuthenticatorCallbacks mock_cb;
  EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  auth_->verify(headers, &mock_cb);

  EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"),
            "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcG"
            "xlLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiYXVkIjoiZXhhbXBsZV9zZXJ2"
            "aWNlIn0");
  // Verify the token is removed.
  EXPECT_FALSE(headers.Authorization());

  EXPECT_EQ(mock_pubkey.called_count(), 1);
}

// Verifies that a JWT with aud: http://example_service/ is matched to
// example_service in config.
TEST_F(AuthenticatorTest, TestOkJWTAudService) {
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  // Test OK pubkey and its cache
  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodTokenAudHasProtocolScheme}};

  MockAuthenticatorCallbacks mock_cb;
  EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  auth_->verify(headers, &mock_cb);

  EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"),
            "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGx"
            "lLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiYXVkIjoiaHR0cDovL2V4YW1wbG"
            "Vfc2VydmljZS8ifQ");
  // Verify the token is removed.
  EXPECT_FALSE(headers.Authorization());

  EXPECT_EQ(mock_pubkey.called_count(), 1);
}

// Verifies that a JWT with aud: https://example_service1/ is matched to
// a JWT with aud: http://example_service1 in config.
TEST_F(AuthenticatorTest, TestOkJWTAudService1) {
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  // Test OK pubkey and its cache
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodTokenAudService1}};

  MockAuthenticatorCallbacks mock_cb;
  EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  auth_->verify(headers, &mock_cb);

  EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"),
            "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGx"
            "lLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiYXVkIjoiaHR0cHM6Ly9leGFtcG"
            "xlX3NlcnZpY2UxLyJ9");
  // Verify the token is removed.
  EXPECT_FALSE(headers.Authorization());

  EXPECT_EQ(mock_pubkey.called_count(), 1);
}

// Verifies that a JWT with aud: http://example_service2 is matched to
// a JWT with aud: https://example_service2/ in config.
TEST_F(AuthenticatorTest, TestOkJWTAudService2) {
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  // Test OK pubkey and its cache
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodTokenAudService2}};

  MockAuthenticatorCallbacks mock_cb;
  EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  auth_->verify(headers, &mock_cb);

  EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"),
            "eyJpc3MiOiJodHRwczovL2V4YW1wbGUuY29tIiwic3ViIjoidGVzdEBleGFtcGx"
            "lLmNvbSIsImV4cCI6MjAwMTAwMTAwMSwiYXVkIjoiaHR0cDovL2V4YW1wbG"
            "Vfc2VydmljZTIifQ");
  // Verify the token is removed.
  EXPECT_FALSE(headers.Authorization());

  EXPECT_EQ(mock_pubkey.called_count(), 1);
}

TEST_F(AuthenticatorTest, TestForwardJwt) {
  // Confit forward_jwt flag
  config_.mutable_rules(0)->set_forward(true);
  // Re-create store and auth objects.
  store_factory_ = ::std::make_shared<DataStoreFactory>(config_, mock_factory_ctx_);
  auth_ = Authenticator::create(store_factory_);

  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  // Test OK pubkey and its cache
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodToken}};

  MockAuthenticatorCallbacks mock_cb;
  EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  auth_->verify(headers, &mock_cb);

  // Verify the token is NOT removed.
  EXPECT_TRUE(headers.Authorization());
}

TEST_F(AuthenticatorTest, TestMissedJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  }));

  // Empty headers.
  auto headers = Http::TestHeaderMapImpl{};
  auth_->verify(headers, &mock_cb_);
}

TEST_F(AuthenticatorTest, TestMissingJwtWhenAllowMissingOrFailedIsTrue) {
  // In this test, when JWT is missing, the status should still be OK
  // because allow_missing_or_failed is true.
  SetupConfig(ExampleConfigWithJwtAndAllowMissingOrFailed);
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  // Empty headers.
  auto headers = Http::TestHeaderMapImpl{};
  auth_->verify(headers, &mock_cb_);
}

TEST_F(AuthenticatorTest, TestInValidJwtWhenAllowMissingOrFailedIsTrue) {
  // In this test, when JWT is invalid, the status should still be OK
  // because allow_missing_or_failed is true.
  SetupConfig(ExampleConfigWithJwtAndAllowMissingOrFailed);
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  std::string token = "invalidToken";
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + token}};
  auth_->verify(headers, &mock_cb_);
}

TEST_F(AuthenticatorTest, TestBypassJWT) {
  SetupConfig(BypassConfig);

  // TODO: enable Bypass test
  return;

  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_))
      .WillOnce(Invoke(
          // Empty header, rejected.
          [](const Status& status) { ASSERT_EQ(status, Status::JwtMissed); }))
      .WillOnce(Invoke(
          // CORS header, OK
          [](const Status& status) { ASSERT_EQ(status, Status::Ok); }))
      .WillOnce(Invoke(
          // healthz header, OK
          [](const Status& status) { ASSERT_EQ(status, Status::Ok); }));

  // Empty headers.
  auto empty_headers = Http::TestHeaderMapImpl{};
  auth_->verify(empty_headers, &mock_cb_);

  // CORS headers
  auto cors_headers = Http::TestHeaderMapImpl{{":method", "OPTIONS"}, {":path", "/any/path"}};
  auth_->verify(cors_headers, &mock_cb_);

  // healthz headers
  auto healthz_headers = Http::TestHeaderMapImpl{{":method", "GET"}, {":path", "/healthz"}};
  auth_->verify(healthz_headers, &mock_cb_);
}

TEST_F(AuthenticatorTest, TestInvalidJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtBadFormat);
  }));

  std::string token = "invalidToken";
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + token}};
  auth_->verify(headers, &mock_cb_);
}

TEST_F(AuthenticatorTest, TestInvalidPrefix) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer-invalid"}};
  auth_->verify(headers, &mock_cb_);
}

TEST_F(AuthenticatorTest, TestExpiredJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtExpired);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + ExpiredToken}};
  auth_->verify(headers, &mock_cb_);
}

TEST_F(AuthenticatorTest, TestNonMatchAudJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtAudienceNotAllowed);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + InvalidAudToken}};
  auth_->verify(headers, &mock_cb_);
}

TEST_F(AuthenticatorTest, TestWrongCluster) {
  // Get returns nullptr
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, get(_))
      .WillOnce(Invoke([](const std::string& cluster) -> Upstream::ThreadLocalCluster* {
        EXPECT_EQ(cluster, "pubkey_cluster");
        return nullptr;
      }));

  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodToken}};
  auth_->verify(headers, &mock_cb_);
}

TEST_F(AuthenticatorTest, TestIssuerNotFound) {
  // Create a config with an other issuer.
  SetupConfig(OtherIssuerConfig);

  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtUnknownIssuer);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodToken}};
  auth_->verify(headers, &mock_cb_);
}

TEST_F(AuthenticatorTest, TestPubkeyFetchFail) {
  NiceMock<Http::MockAsyncClient> async_client;
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_))
      .WillOnce(Invoke([&](const std::string& cluster) -> Http::AsyncClient& {
        EXPECT_EQ(cluster, "pubkey_cluster");
        return async_client;
      }));

  Http::MockAsyncClientRequest request(&async_client);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(async_client, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestHeaderMapImpl{
                          {":method", "GET"},
                          {":path", "/pubkey_path"},
                          {":authority", "pubkey_server"},
                      }),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodToken}};
  auth_->verify(headers, &mock_cb_);

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "401"}}}));
  callbacks->onSuccess(std::move(response_message));
}

TEST_F(AuthenticatorTest, TestInvalidPubkey) {
  NiceMock<Http::MockAsyncClient> async_client;
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_))
      .WillOnce(Invoke([&](const std::string& cluster) -> Http::AsyncClient& {
        EXPECT_EQ(cluster, "pubkey_cluster");
        return async_client;
      }));

  Http::MockAsyncClientRequest request(&async_client);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(async_client, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestHeaderMapImpl{
                          {":method", "GET"},
                          {":path", "/pubkey_path"},
                          {":authority", "pubkey_server"},
                      }),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwksParseError);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodToken}};
  auth_->verify(headers, &mock_cb_);

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  response_message->body().reset(new Buffer::OwnedImpl("invalid publik key"));
  callbacks->onSuccess(std::move(response_message));
}

TEST_F(AuthenticatorTest, TestOnDestroy) {
  NiceMock<Http::MockAsyncClient> async_client;
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_))
      .WillOnce(Invoke([&](const std::string& cluster) -> Http::AsyncClient& {
        EXPECT_EQ(cluster, "pubkey_cluster");
        return async_client;
      }));

  Http::MockAsyncClientRequest request(&async_client);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(async_client, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr& message, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            EXPECT_EQ((Http::TestHeaderMapImpl{
                          {":method", "GET"},
                          {":path", "/pubkey_path"},
                          {":authority", "pubkey_server"},
                      }),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  // Cancel is called once.
  EXPECT_CALL(request, cancel()).Times(1);

  // onComplete() should not be called.
  EXPECT_CALL(mock_cb_, onComplete(_)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodToken}};
  auth_->verify(headers, &mock_cb_);

  // Destroy the authenticating process.
  auth_->onDestroy();
}

TEST_F(AuthenticatorTest, TestNoForwardPayloadHeader) {
  // In this config, there is no forward_payload_header
  SetupConfig(ExampleConfigWithoutForwardPayloadHeader);
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodToken}};
  MockAuthenticatorCallbacks mock_cb;
  EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auth_->verify(headers, &mock_cb);

  // Test when forward_payload_header is not set, the output should NOT
  // contain the sec-istio-auth-userinfo header.
  EXPECT_FALSE(headers.has("sec-istio-auth-userinfo"));
}

TEST_F(AuthenticatorTest, TestInlineJwks) {
  // Change the config to use local_jwks.inline_string
  auto rule0 = config_.mutable_rules(0);
  rule0->clear_remote_jwks();
  auto local_jwks = rule0->mutable_local_jwks();
  local_jwks->set_inline_string(PublicKey);

  // recreate store and auth with modified config.
  store_factory_ = ::std::make_shared<DataStoreFactory>(config_, mock_factory_ctx_);
  auth_ = Authenticator::create(store_factory_);

  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, "");
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + GoodToken}};

  MockAuthenticatorCallbacks mock_cb;
  EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  auth_->verify(headers, &mock_cb);
  EXPECT_EQ(mock_pubkey.called_count(), 0);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
