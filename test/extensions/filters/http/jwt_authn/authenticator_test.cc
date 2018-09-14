#include "common/http/message_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/common/jwks_fetcher.h"
#include "extensions/filters/http/jwt_authn/authenticator.h"
#include "extensions/filters/http/jwt_authn/filter_config.h"

#include "test/extensions/filters/http/common/mock.h"
#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using Envoy::Extensions::HttpFilters::Common::JwksFetcher;
using Envoy::Extensions::HttpFilters::Common::JwksFetcherPtr;
using Envoy::Extensions::HttpFilters::Common::MockJwksFetcher;
using ::google::jwt_verify::Jwks;
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

  void CreateAuthenticator(
      ::google::jwt_verify::CheckAudience* check_audience = nullptr,
      const absl::optional<std::string>& provider = absl::optional<std::string>{ProviderName}) {
    filter_config_ = ::std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
    fetcher_ = new MockJwksFetcher;
    fetcherPtr_.reset(fetcher_);
    auth_ = Authenticator::create(
        check_audience, provider, !provider, filter_config_->getCache().getJwksCache(),
        filter_config_->cm(), [this](Upstream::ClusterManager&) { return std::move(fetcherPtr_); });
    jwks_ = Jwks::createFrom(PublicKey, Jwks::JWKS);
    EXPECT_TRUE(jwks_->getStatus() == Status::Ok);
  }

  JwtAuthentication proto_config_;
  FilterConfigSharedPtr filter_config_;
  MockJwksFetcher* fetcher_;
  JwksFetcherPtr fetcherPtr_;
  AuthenticatorPtr auth_;
  ::google::jwt_verify::JwksPtr jwks_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
};

// This test validates a good JWT authentication with a remote Jwks.
// It also verifies Jwks cache with 10 JWT authentications, but only one Jwks fetch.
TEST_F(AuthenticatorTest, TestOkJWTandCache) {
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  // Test OK pubkey and its cache
  for (int i = 0; i < 10; i++) {
    auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};

    std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
      ASSERT_EQ(status, Status::Ok);
    };
    auto tokens = filter_config_->getExtractor().extract(headers);
    auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));

    EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"), ExpectedPayloadValue);
    // Verify the token is removed.
    EXPECT_FALSE(headers.Authorization());
  }
}

// This test verifies the Jwt is forwarded if "forward" flag is set.
TEST_F(AuthenticatorTest, TestForwardJwt) {
  // Confit forward_jwt flag
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_forward(true);
  CreateAuthenticator();
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  // Test OK pubkey and its cache
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};

  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));

  // Verify the token is NOT removed.
  EXPECT_TRUE(headers.Authorization());
}

// This test verifies the Jwt with non existing kid
TEST_F(AuthenticatorTest, TestJwtWithNonExistKid) {
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  // Test OK pubkey and its cache
  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(NonExistKidToken)}};

  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtVerificationFail);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));
}

// This test verifies if Jwt is missing, proper status is called.
TEST_F(AuthenticatorTest, TestMissedJWT) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);

  // Empty headers.
  auto headers = Http::TestHeaderMapImpl{};
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));
}

// This test verifies if Jwt is invalid, JwtBadFormat status is returned.
TEST_F(AuthenticatorTest, TestInvalidJWT) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);

  std::string token = "invalidToken";
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + token}};
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtBadFormat);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));
}

// This test verifies if Authorization header has invalid prefix, JwtMissed status is returned
TEST_F(AuthenticatorTest, TestInvalidPrefix) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer-invalid"}};
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));
}

// This test verifies when a JWT is non-expiring without audience specified, JwtAudienceNotAllowed
// is returned.
TEST_F(AuthenticatorTest, TestNonExpiringJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(NonExpiringToken)}};
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtAudienceNotAllowed);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));
}

// This test verifies when a JWT is expired, JwtExpired status is returned.
TEST_F(AuthenticatorTest, TestExpiredJWT) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(ExpiredToken)}};
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtExpired);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));
}

// This test verifies when a JWT is not yet valid, JwtNotYetValid status is returned.
TEST_F(AuthenticatorTest, TestNotYetValidJWT) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtNotYetValid);
  };

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(NotYetValidToken)}};
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));
}

// This test verifies when an inline JWKS is misconfigured, JwksNoValidKeys is returns
TEST_F(AuthenticatorTest, TestInvalidLocalJwks) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.clear_remote_jwks();
  provider.mutable_local_jwks()->set_inline_string("invalid");
  CreateAuthenticator();

  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwksNoValidKeys);
  };

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));
}

// This test verifies when a JWT is with invalid audience, JwtAudienceNotAllowed is returned.
TEST_F(AuthenticatorTest, TestNonMatchAudJWT) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtAudienceNotAllowed);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));
}

// This test verifies when Jwt issuer is not configured, JwtUnknownIssuer is returned.
TEST_F(AuthenticatorTest, TestIssuerNotFound) {
  // Create a config with an other issuer.
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_issuer("other_issuer");
  CreateAuthenticator();

  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwtUnknownIssuer);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));
}

// This test verifies that when Jwks fetching fails, JwksFetchFail status is returned.
TEST_F(AuthenticatorTest, TestPubkeyFetchFail) {
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .WillOnce(
          Invoke([](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksError(JwksFetcher::JwksReceiver::Failure::InvalidJwks);
          }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "401"}}}));
}

// This test verifies when a Jwks fetching is not completed yet, but onDestory() is called,
// onComplete() callback should not be called, but internal request->cancel() should be called.
// Most importantly, no crash.
TEST_F(AuthenticatorTest, TestOnDestroy) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(1);

  // Cancel is called once.
  EXPECT_CALL(*fetcher_, cancel()).Times(1);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auto tokens = filter_config_->getExtractor().extract(headers);
  // callback should not be called.
  std::function<void(const Status&)> on_complete_cb = [](const Status&) { ASSERT_TRUE(false); };
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));

  // Destroy the authenticating process.
  auth_->onDestroy();
}

// This test verifies if "forward_playload_header" is empty, payload is not forwarded.
TEST_F(AuthenticatorTest, TestNoForwardPayloadHeader) {
  // In this config, there is no forward_payload_header
  auto& provider0 = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider0.clear_forward_payload_header();
  CreateAuthenticator();
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));

  // Test when forward_payload_header is not set, the output should NOT
  // contain the sec-istio-auth-userinfo header.
  EXPECT_FALSE(headers.has("sec-istio-auth-userinfo"));
}

// This test verifies that allow failed authenticator will verify all tokens.
TEST_F(AuthenticatorTest, TestAllowFailedMultipleTokens) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  std::vector<std::string> names = {"a", "b", "c"};
  for (const auto& it : names) {
    auto header = provider.add_from_headers();
    header->set_name(it);
    header->set_value_prefix("Bearer ");
  }

  CreateAuthenticator(nullptr, absl::nullopt);
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  auto headers = Http::TestHeaderMapImpl{
      {"a", "Bearer " + std::string(ExpiredToken)},
      {"b", "Bearer " + std::string(GoodToken)},
      {"c", "Bearer " + std::string(InvalidAudToken)},
      {":path", "/"},
  };
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));

  EXPECT_TRUE(headers.has("a"));
  EXPECT_FALSE(headers.has("b"));
  EXPECT_TRUE(headers.has("c"));

  headers = Http::TestHeaderMapImpl{
      {"a", "Bearer " + std::string(GoodToken)},
      {"b", "Bearer " + std::string(GoodToken)},
      {"c", "Bearer " + std::string(GoodToken)},
      {":path", "/"},
  };
  on_complete_cb = [](const Status& status) { ASSERT_EQ(status, Status::Ok); };
  tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));

  EXPECT_FALSE(headers.has("a"));
  EXPECT_FALSE(headers.has("b"));
  EXPECT_FALSE(headers.has("c"));
}

// This test verifies that allow failed authenticator will verify all tokens.
TEST_F(AuthenticatorTest, TestAllowFailedMultipleIssuers) {
  auto& provider = (*proto_config_.mutable_providers())["other_provider"];
  provider.set_issuer("https://other.com");
  provider.add_audiences("other_service");
  auto& uri = *provider.mutable_remote_jwks()->mutable_http_uri();
  uri.set_uri("https://pubkey_server/pubkey_path");
  uri.set_cluster("pubkey_cluster");
  auto header = provider.add_from_headers();
  header->set_name("expired-auth");
  header->set_value_prefix("Bearer ");
  header = provider.add_from_headers();
  header->set_name("other-auth");
  header->set_value_prefix("Bearer ");

  CreateAuthenticator(nullptr, absl::nullopt);
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .Times(2)
      .WillRepeatedly(
          Invoke([](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            ::google::jwt_verify::JwksPtr jwks = Jwks::createFrom(PublicKey, Jwks::JWKS);
            EXPECT_TRUE(jwks->getStatus() == Status::Ok);
            receiver.onJwksSuccess(std::move(jwks));
          }));

  auto headers = Http::TestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {"expired-auth", "Bearer " + std::string(ExpiredToken)},
      {"other-auth", "Bearer " + std::string(OtherGoodToken)},
      {":path", "/"},
  };
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  };
  auto tokens = filter_config_->getExtractor().extract(headers);
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));

  EXPECT_FALSE(headers.has("Authorization"));
  EXPECT_TRUE(headers.has("expired-auth"));
  EXPECT_FALSE(headers.has("other-auth"));
}

// Test checks that supplying a CheckAudience to auth will override the one in JwksCache.
TEST_F(AuthenticatorTest, TestCustomCheckAudience) {
  auto check_audience = std::make_unique<::google::jwt_verify::CheckAudience>(
      std::vector<std::string>{"invalid_service"});
  CreateAuthenticator(check_audience.get());
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  auto tokens = filter_config_->getExtractor().extract(headers);
  std::function<void(const Status&)> on_complete_cb = [](const Status& status) {
    ASSERT_EQ(Status::Ok, status);
  };
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));

  headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  tokens = filter_config_->getExtractor().extract(headers);
  on_complete_cb = [](const Status& status) { ASSERT_EQ(Status::JwtAudienceNotAllowed, status); };
  auth_->verify(headers, std::move(tokens), std::move(on_complete_cb));
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
