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

  void CreateAuthenticator(::google::jwt_verify::CheckAudience* check_audience = nullptr,
                           const absl::optional<std::string>& provider =
                               absl::make_optional<std::string>(ProviderName)) {
    filter_config_ = ::std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
    raw_fetcher_ = new MockJwksFetcher;
    fetcher_.reset(raw_fetcher_);
    auth_ = Authenticator::create(check_audience, provider, !provider,
                                  filter_config_->getCache().getJwksCache(), filter_config_->cm(),
                                  [this](Upstream::ClusterManager&) { return std::move(fetcher_); },
                                  filter_config_->timeSource());
    jwks_ = Jwks::createFrom(PublicKey, Jwks::JWKS);
    EXPECT_TRUE(jwks_->getStatus() == Status::Ok);
  }

  void expectVerifyStatus(Status expected_status, Http::HeaderMap& headers) {
    std::function<void(const Status&)> on_complete_cb = [&expected_status](const Status& status) {
      ASSERT_EQ(status, expected_status);
    };
    auto set_payload_cb = [this](const std::string& name, const ProtobufWkt::Struct& payload) {
      out_name_ = name;
      out_payload_ = payload;
    };
    auto tokens = filter_config_->getExtractor().extract(headers);
    auth_->verify(headers, std::move(tokens), std::move(set_payload_cb), std::move(on_complete_cb));
  }

  JwtAuthentication proto_config_;
  FilterConfigSharedPtr filter_config_;
  MockJwksFetcher* raw_fetcher_;
  JwksFetcherPtr fetcher_;
  AuthenticatorPtr auth_;
  ::google::jwt_verify::JwksPtr jwks_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  std::string out_name_;
  ProtobufWkt::Struct out_payload_;
};

// This test validates a good JWT authentication with a remote Jwks.
// It also verifies Jwks cache with 10 JWT authentications, but only one Jwks fetch.
TEST_F(AuthenticatorTest, TestOkJWTandCache) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  // Test OK pubkey and its cache
  for (int i = 0; i < 10; i++) {
    auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};

    expectVerifyStatus(Status::Ok, headers);

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
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  // Test OK pubkey and its cache
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};

  expectVerifyStatus(Status::Ok, headers);

  // Verify the token is NOT removed.
  EXPECT_TRUE(headers.Authorization());

  // Payload not set by default
  EXPECT_EQ(out_name_, "");
}

// This test verifies the Jwt payload is set.
TEST_F(AuthenticatorTest, TestSetPayload) {
  // Confit payload_in_metadata flag
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_payload_in_metadata(
      "my_payload");
  CreateAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  // Test OK pubkey and its cache
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};

  expectVerifyStatus(Status::Ok, headers);

  // Payload is set
  EXPECT_EQ(out_name_, "my_payload");

  ProtobufWkt::Struct expected_payload;
  MessageUtil::loadFromJson(ExpectedPayloadJSON, expected_payload);
  EXPECT_TRUE(TestUtility::protoEqual(out_payload_, expected_payload));
}

// This test verifies the Jwt with non existing kid
TEST_F(AuthenticatorTest, TestJwtWithNonExistKid) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  // Test OK pubkey and its cache
  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(NonExistKidToken)}};

  expectVerifyStatus(Status::JwtVerificationFail, headers);
}

// This test verifies if Jwt is missing, proper status is called.
TEST_F(AuthenticatorTest, TestMissedJWT) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  // Empty headers.
  auto headers = Http::TestHeaderMapImpl{};

  expectVerifyStatus(Status::JwtMissed, headers);
}

// This test verifies if Jwt is invalid, JwtBadFormat status is returned.
TEST_F(AuthenticatorTest, TestInvalidJWT) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  std::string token = "invalidToken";
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + token}};
  expectVerifyStatus(Status::JwtBadFormat, headers);
}

// This test verifies if Authorization header has invalid prefix, JwtMissed status is returned
TEST_F(AuthenticatorTest, TestInvalidPrefix) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer-invalid"}};
  expectVerifyStatus(Status::JwtMissed, headers);
}

// This test verifies when a JWT is non-expiring without audience specified, JwtAudienceNotAllowed
// is returned.
TEST_F(AuthenticatorTest, TestNonExpiringJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(NonExpiringToken)}};
  expectVerifyStatus(Status::JwtAudienceNotAllowed, headers);
}

// This test verifies when a JWT is expired, JwtExpired status is returned.
TEST_F(AuthenticatorTest, TestExpiredJWT) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(ExpiredToken)}};
  expectVerifyStatus(Status::JwtExpired, headers);
}

// This test verifies when a JWT is not yet valid, JwtNotYetValid status is returned.
TEST_F(AuthenticatorTest, TestNotYetValidJWT) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(NotYetValidToken)}};
  expectVerifyStatus(Status::JwtNotYetValid, headers);
}

// This test verifies when an inline JWKS is misconfigured, JwksNoValidKeys is returns
TEST_F(AuthenticatorTest, TestInvalidLocalJwks) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.clear_remote_jwks();
  provider.mutable_local_jwks()->set_inline_string("invalid");
  CreateAuthenticator();

  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::JwksNoValidKeys, headers);
}

// This test verifies when a JWT is with invalid audience, JwtAudienceNotAllowed is returned.
TEST_F(AuthenticatorTest, TestNonMatchAudJWT) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  expectVerifyStatus(Status::JwtAudienceNotAllowed, headers);
}

// This test verifies when Jwt issuer is not configured, JwtUnknownIssuer is returned.
TEST_F(AuthenticatorTest, TestIssuerNotFound) {
  // Create a config with an other issuer.
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_issuer("other_issuer");
  CreateAuthenticator();

  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(0);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::JwtUnknownIssuer, headers);
}

// This test verifies that when Jwks fetching fails, JwksFetchFail status is returned.
TEST_F(AuthenticatorTest, TestPubkeyFetchFail) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(
          Invoke([](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksError(JwksFetcher::JwksReceiver::Failure::InvalidJwks);
          }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::JwksFetchFail, headers);

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "401"}}}));
}

// This test verifies when a Jwks fetching is not completed yet, but onDestory() is called,
// onComplete() callback should not be called, but internal request->cancel() should be called.
// Most importantly, no crash.
TEST_F(AuthenticatorTest, TestOnDestroy) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _)).Times(1);

  // Cancel is called once.
  EXPECT_CALL(*raw_fetcher_, cancel()).Times(1);

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auto tokens = filter_config_->getExtractor().extract(headers);
  // callback should not be called.
  std::function<void(const Status&)> on_complete_cb = [](const Status&) { FAIL(); };
  auth_->verify(headers, std::move(tokens), nullptr, std::move(on_complete_cb));

  // Destroy the authenticating process.
  auth_->onDestroy();
}

// This test verifies if "forward_playload_header" is empty, payload is not forwarded.
TEST_F(AuthenticatorTest, TestNoForwardPayloadHeader) {
  // In this config, there is no forward_payload_header
  auto& provider0 = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider0.clear_forward_payload_header();
  CreateAuthenticator();
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::Ok, headers);

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
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
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
  expectVerifyStatus(Status::Ok, headers);

  EXPECT_TRUE(headers.has("a"));
  EXPECT_FALSE(headers.has("b"));
  EXPECT_TRUE(headers.has("c"));

  headers = Http::TestHeaderMapImpl{
      {"a", "Bearer " + std::string(GoodToken)},
      {"b", "Bearer " + std::string(GoodToken)},
      {"c", "Bearer " + std::string(GoodToken)},
      {":path", "/"},
  };
  expectVerifyStatus(Status::Ok, headers);

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
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
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
  expectVerifyStatus(Status::Ok, headers);

  EXPECT_FALSE(headers.has("Authorization"));
  EXPECT_TRUE(headers.has("expired-auth"));
  EXPECT_FALSE(headers.has("other-auth"));
}

// Test checks that supplying a CheckAudience to auth will override the one in JwksCache.
TEST_F(AuthenticatorTest, TestCustomCheckAudience) {
  auto check_audience = std::make_unique<::google::jwt_verify::CheckAudience>(
      std::vector<std::string>{"invalid_service"});
  CreateAuthenticator(check_audience.get());
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  expectVerifyStatus(Status::Ok, headers);

  headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::JwtAudienceNotAllowed, headers);
}

// This test verifies that when invalid JWKS is fetched, an JWKS error status is returned.
TEST_F(AuthenticatorTest, TestInvalidPubkeyKey) {
  EXPECT_CALL(*raw_fetcher_, fetch(_, _))
      .WillOnce(
          Invoke([](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            auto jwks = Jwks::createFrom(PublicKey, Jwks::PEM);
            receiver.onJwksSuccess(std::move(jwks));
          }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  expectVerifyStatus(Status::JwksPemBadBase64, headers);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
