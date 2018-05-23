#include "common/http/message_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/authenticator.h"

#include "test/extensions/filters/http/jwt_authn/test_common.h"
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

} // namespace

class MockAuthenticatorCallbacks : public Authenticator::Callbacks {
public:
  MOCK_METHOD1(onComplete, void(const Status& status));
};

class AuthenticatorTest : public ::testing::Test {
public:
  void SetUp() {
    MessageUtil::loadFromYaml(ExampleConfig, config_pb_);
    CreateAuthenticator();
  }

  void CreateAuthenticator() {
    config_obj_ = ::std::make_shared<Config>(config_pb_, mock_factory_ctx_);
    auth_ = Authenticator::create(config_obj_);
  }

  JwtAuthentication config_pb_;
  std::unique_ptr<Authenticator> auth_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  ConfigSharedPtr config_obj_;
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

TEST_F(AuthenticatorTest, TestForwardJwt) {
  // Confit forward_jwt flag
  config_pb_.mutable_rules(0)->set_forward(true);
  CreateAuthenticator();

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
  config_pb_.set_allow_missing_or_failed(true);
  CreateAuthenticator();

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
  config_pb_.set_allow_missing_or_failed(true);
  CreateAuthenticator();

  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  std::string token = "invalidToken";
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + token}};
  auth_->verify(headers, &mock_cb_);
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
  config_pb_.mutable_rules(0)->set_issuer("other_issuer");
  CreateAuthenticator();

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
  auto rule0 = config_pb_.mutable_rules(0);
  rule0->clear_forward_payload_header();
  CreateAuthenticator();

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

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
