#include "common/http/message_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/authenticator.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
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

class AuthenticatorTest : public ::testing::Test {
public:
  void SetUp() {
    MessageUtil::loadFromYaml(ExampleConfig, proto_config_);
    CreateAuthenticator();
  }

  void CreateAuthenticator() {
    filter_config_ = ::std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
    auth_ = Authenticator::create(filter_config_);
  }

  JwtAuthentication proto_config_;
  FilterConfigSharedPtr filter_config_;
  std::unique_ptr<Authenticator> auth_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
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

// This test validates a good JWT authentication with a remote Jwks.
// It also verifies Jwks cache with 10 JWT authentications, but only one Jwks fetch.
TEST_F(AuthenticatorTest, TestOkJWTandCache) {
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  // Test OK pubkey and its cache
  for (int i = 0; i < 10; i++) {
    auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};

    MockAuthenticatorCallbacks mock_cb;
    EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
      ASSERT_EQ(status, Status::Ok);
    }));

    auth_->verify(headers, &mock_cb);

    EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"), ExpectedPayloadValue);
    // Verify the token is removed.
    EXPECT_FALSE(headers.Authorization());
  }

  EXPECT_EQ(mock_pubkey.called_count(), 1);
}

// This test verifies the Jwt is forwarded if "forward" flag is set.
TEST_F(AuthenticatorTest, TestForwardJwt) {
  // Confit forward_jwt flag
  proto_config_.mutable_rules(0)->set_forward(true);
  CreateAuthenticator();

  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  // Test OK pubkey and its cache
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};

  MockAuthenticatorCallbacks mock_cb;
  EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  auth_->verify(headers, &mock_cb);

  // Verify the token is NOT removed.
  EXPECT_TRUE(headers.Authorization());
}

// This test verifies the Jwt with non existing kid
TEST_F(AuthenticatorTest, TestJwtWithNonExistKid) {
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  // Test OK pubkey and its cache
  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(NonExistKidToken)}};

  MockAuthenticatorCallbacks mock_cb;
  EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtVerificationFail);
  }));

  auth_->verify(headers, &mock_cb);
}

// This test verifies if Jwt is missing, proper status is called.
TEST_F(AuthenticatorTest, TestMissedJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  }));

  // Empty headers.
  auto headers = Http::TestHeaderMapImpl{};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies if Jwt is missing but allow_missing_or_fail is true, Status::OK is returned.
TEST_F(AuthenticatorTest, TestMissingJwtWhenAllowMissingOrFailedIsTrue) {
  // In this test, when JWT is missing, the status should still be OK
  // because allow_missing_or_failed is true.
  proto_config_.set_allow_missing_or_failed(true);
  CreateAuthenticator();

  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  // Empty headers.
  auto headers = Http::TestHeaderMapImpl{};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies if Jwt verifiecation fails but allow_missing_or_fail is true, Status::OK is
// returned.
TEST_F(AuthenticatorTest, TestInValidJwtWhenAllowMissingOrFailedIsTrue) {
  // In this test, when JWT is invalid, the status should still be OK
  // because allow_missing_or_failed is true.
  proto_config_.set_allow_missing_or_failed(true);
  CreateAuthenticator();

  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));

  std::string token = "invalidToken";
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + token}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies if Jwt is invalid, JwtBadFormat status is returned.
TEST_F(AuthenticatorTest, TestInvalidJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtBadFormat);
  }));

  std::string token = "invalidToken";
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + token}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies if Authorization header has invalid prefix, JwtMissed status is returned
TEST_F(AuthenticatorTest, TestInvalidPrefix) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer-invalid"}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies when a JWT is expired, JwtExpired status is returned.
TEST_F(AuthenticatorTest, TestExpiredJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtExpired);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(ExpiredToken)}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies when a JWT is with invalid audience, JwtAudienceNotAllowed is returned.
TEST_F(AuthenticatorTest, TestNonMatchAudJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtAudienceNotAllowed);
  }));

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies when invalid cluster is configured for remote jwks, JwksFetchFail is returned.
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

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies when Jwt issuer is not configured, JwtUnknownIssuer is returned.
TEST_F(AuthenticatorTest, TestIssuerNotFound) {
  // Create a config with an other issuer.
  proto_config_.mutable_rules(0)->set_issuer("other_issuer");
  CreateAuthenticator();

  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtUnknownIssuer);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies that when Jwks fetching fails, JwksFetchFail status is returned.
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
                          {":path", "/pubkey_path"},
                          {":authority", "pubkey_server"},
                          {":method", "GET"},
                      }),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auth_->verify(headers, &mock_cb_);

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "401"}}}));
  callbacks->onSuccess(std::move(response_message));
}

// This test verifies that when Jwks fetching return empty body
TEST_F(AuthenticatorTest, TestPubkeyFetchFailWithEmptyBody) {
  Http::MockAsyncClientRequest request(&mock_factory_ctx_.cluster_manager_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr&, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callbacks = &cb;
            return &request;
          }));

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auth_->verify(headers, &mock_cb_);

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  callbacks->onSuccess(std::move(response_message));
}

// This test verifies that when Jwks fetching returned with onFailure
TEST_F(AuthenticatorTest, TestPubkeyFetchFailWithOnFailure) {
  Http::MockAsyncClientRequest request(&mock_factory_ctx_.cluster_manager_.async_client_);
  Http::AsyncClient::Callbacks* callbacks;
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_.async_client_, send_(_, _, _))
      .WillOnce(Invoke(
          [&](Http::MessagePtr&, Http::AsyncClient::Callbacks& cb,
              const absl::optional<std::chrono::milliseconds>&) -> Http::AsyncClient::Request* {
            callbacks = &cb;
            return &request;
          }));

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auth_->verify(headers, &mock_cb_);

  callbacks->onFailure(Http::AsyncClient::FailureReason::Reset);
}

// This test verifies when an invalid Jwks is fetched, JwksParseError status is returned.
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
                          {":path", "/pubkey_path"},
                          {":authority", "pubkey_server"},
                          {":method", "GET"},
                      }),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwksParseError);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auth_->verify(headers, &mock_cb_);

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "200"}}}));
  response_message->body().reset(new Buffer::OwnedImpl("invalid publik key"));
  callbacks->onSuccess(std::move(response_message));
}

// This test verifies when a Jwks fetching is not completed yet, but onDestory() is called,
// onComplete() callback should not be called, but internal request->cancel() should be called.
// Most importantly, no crash.
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
                          {":path", "/pubkey_path"},
                          {":authority", "pubkey_server"},
                          {":method", "GET"},
                      }),
                      message->headers());
            callbacks = &cb;
            return &request;
          }));

  // Cancel is called once.
  EXPECT_CALL(request, cancel()).Times(1);

  // onComplete() should not be called.
  EXPECT_CALL(mock_cb_, onComplete(_)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auth_->verify(headers, &mock_cb_);

  // Destroy the authenticating process.
  auth_->onDestroy();
}

// This test verifies if "forward_playload_header" is empty, payload is not forwarded.
TEST_F(AuthenticatorTest, TestNoForwardPayloadHeader) {
  // In this config, there is no forward_payload_header
  auto rule0 = proto_config_.mutable_rules(0);
  rule0->clear_forward_payload_header();
  CreateAuthenticator();

  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
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
