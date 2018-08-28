#include "common/http/message_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/jwt_authn/authenticator.h"
#include "extensions/filters/http/jwt_authn/filter_config.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::google::jwt_verify::Status;
using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;

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

  void CreateAuthenticator(bool allow_failed = false) {
    filter_config_ = ::std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
    auth_ = Authenticator::create({}, absl::nullopt, allow_failed,
                                  filter_config_->getProtoConfig().providers(),
                                  filter_config_->getCache().getJwksCache(), filter_config_->cm());
  }

  JwtAuthentication proto_config_;
  FilterConfigSharedPtr filter_config_;
  std::unique_ptr<Authenticator> auth_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
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

    std::function<void(const Status&)> mock_cb = [](const Status& status) {
      ASSERT_EQ(status, Status::Ok);
    };
    auto tokens = filter_config_->getExtractor().extract(headers);
    auth_->verify(headers, std::move(tokens), std::move(mock_cb));

    EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"), ExpectedPayloadValue);
    // Verify the token is removed.
    EXPECT_FALSE(headers.Authorization());
  }

  EXPECT_EQ(mock_pubkey.called_count(), 1);
}

// This test verifies the Jwt is forwarded if "forward" flag is set.
TEST_F(AuthenticatorTest, TestForwardJwt) {
  // Confit forward_jwt flag
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_forward(true);
  CreateAuthenticator();

  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  // Test OK pubkey and its cache
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};

  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));

  // Verify the token is NOT removed.
  EXPECT_TRUE(headers.Authorization());
}

// This test verifies the Jwt with non existing kid
TEST_F(AuthenticatorTest, TestJwtWithNonExistKid) {
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  // Test OK pubkey and its cache
  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(NonExistKidToken)}};

  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtVerificationFail);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));
}

// This test verifies if Jwt is missing, proper status is called.
TEST_F(AuthenticatorTest, TestMissedJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);

  // Empty headers.
  auto headers = Http::TestHeaderMapImpl{};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));
}

// This test verifies if Jwt is invalid, JwtBadFormat status is returned.
TEST_F(AuthenticatorTest, TestInvalidJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);

  std::string token = "invalidToken";
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + token}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtBadFormat);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));
}

// This test verifies if Authorization header has invalid prefix, JwtMissed status is returned
TEST_F(AuthenticatorTest, TestInvalidPrefix) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer-invalid"}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));
}

// This test verifies when a JWT is non-expiring without audience specified, JwtAudienceNotAllowed
// is returned.
TEST_F(AuthenticatorTest, TestNonExpiringJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(NonExpiringToken)}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtAudienceNotAllowed);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));
}

// This test verifies when a JWT is expired, JwtExpired status is returned.
TEST_F(AuthenticatorTest, TestExpiredJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(ExpiredToken)}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtExpired);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));
}

// This test verifies when a JWT is with invalid audience, JwtAudienceNotAllowed is returned.
TEST_F(AuthenticatorTest, TestNonMatchAudJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtAudienceNotAllowed);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));
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

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));
}

// This test verifies when Jwt issuer is not configured, JwtUnknownIssuer is returned.
TEST_F(AuthenticatorTest, TestIssuerNotFound) {
  // Create a config with an other issuer.
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_issuer("other_issuer");
  CreateAuthenticator();

  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtUnknownIssuer);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));
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

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));

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

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));

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

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));

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

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwksParseError);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));

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

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auto tokens = filter_config_->getExtractor().extract(headers);
  // callback should not be called.
  std::function<void(const Status&)> mock_cb = [](const Status&) { ASSERT_TRUE(false); };
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));

  // Destroy the authenticating process.
  auth_->onDestroy();
}

// This test verifies if "forward_playload_header" is empty, payload is not forwarded.
TEST_F(AuthenticatorTest, TestNoForwardPayloadHeader) {
  // In this config, there is no forward_payload_header
  auto& provider0 = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider0.clear_forward_payload_header();
  CreateAuthenticator();

  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));

  // Test when forward_payload_header is not set, the output should NOT
  // contain the sec-istio-auth-userinfo header.
  EXPECT_FALSE(headers.has("sec-istio-auth-userinfo"));
}

// This test verifies that path match prefix, and JWT authenticates.
TEST_F(AuthenticatorTest, TestPrefixPathMatch) {
  auto& rule = *proto_config_.add_rules();
  rule.mutable_match()->set_prefix("/abc");
  rule.mutable_requires()->set_provider_name(ProviderName);
  CreateAuthenticator();

  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  auto headers = Http::TestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {":path", "/abc/xyz"},
  };

  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));

  EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"), ExpectedPayloadValue);
  // Verify the token is removed.
  EXPECT_FALSE(headers.Authorization());
  EXPECT_EQ(mock_pubkey.called_count(), 1);
}

// This test verifies that path match regex, and JWT authenticates.
TEST_F(AuthenticatorTest, TestRegexPathMatch) {
  auto& rule = *proto_config_.add_rules();
  rule.mutable_match()->set_regex("/b[oi]t");
  rule.mutable_requires()->set_provider_name(ProviderName);
  CreateAuthenticator();

  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  auto headers = Http::TestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {":path", "/bit?x=y"},
  };

  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));

  EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"), ExpectedPayloadValue);
  // Verify the token is removed.
  EXPECT_FALSE(headers.Authorization());
  EXPECT_EQ(mock_pubkey.called_count(), 1);
}

// This test verifies that allow failed authenticator will verify all tokens.
TEST_F(AuthenticatorTest, TestMultipleTokenProviderName) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  std::vector<std::string> names = {"a", "b", "c"};
  for (const auto& it : names) {
    auto header = provider.add_from_headers();
    header->set_name(it);
    header->set_value_prefix("Bearer ");
  }

  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  auto headers = Http::TestHeaderMapImpl{
      {"a", "Bearer " + std::string(ExpiredToken)},
      {"b", "Bearer " + std::string(GoodToken)},
      {"c", "Bearer " + std::string(InvalidAudToken)},
      {":path", "/"},
  };
  CreateAuthenticator(true);
  std::function<void(const Status&)> mock_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtExpired);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));

  EXPECT_TRUE(headers.has("a"));
  EXPECT_FALSE(headers.has("b"));
  EXPECT_TRUE(headers.has("c"));

  headers = Http::TestHeaderMapImpl{
      {"a", "Bearer " + std::string(GoodToken)},
      {"b", "Bearer " + std::string(GoodToken)},
      {"c", "Bearer " + std::string(GoodToken)},
      {":path", "/"},
  };
  mock_cb = [](const Status& status) { ASSERT_EQ(status, Status::Ok); };
  tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(mock_cb));

  EXPECT_FALSE(headers.has("a"));
  EXPECT_FALSE(headers.has("b"));
  EXPECT_FALSE(headers.has("c"));
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
