#include "common/http/message_impl.h"
#include "common/protobuf/utility.h"

#include "extensions/filters/http/common/jwks_fetcher.h"
#include "extensions/filters/http/jwt_authn/authenticator.h"

#include "test/extensions/filters/http/common/mock.h"
#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using Envoy::Extensions::HttpFilters::Common::JwksFetcher;
using Envoy::Extensions::HttpFilters::Common::JwksFetcherPtr;
using Envoy::Extensions::HttpFilters::Common::MockJwksFetcher;
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

  void CreateAuthenticator() {
    filter_config_ = ::std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
    fetcher_ = new MockJwksFetcher;
    fetcherPtr_.reset(fetcher_);
    auth_ = Authenticator::create(
        filter_config_, [this](Upstream::ClusterManager&) { return std::move(fetcherPtr_); });
    jwks_ = ::google::jwt_verify::Jwks::createFrom(PublicKey, ::google::jwt_verify::Jwks::JWKS);
    EXPECT_TRUE(jwks_->getStatus() == Status::Ok);
  }

  JwtAuthentication proto_config_;
  FilterConfigSharedPtr filter_config_;
  MockJwksFetcher* fetcher_;
  JwksFetcherPtr fetcherPtr_;
  AuthenticatorPtr auth_;
  ::google::jwt_verify::JwksPtr jwks_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  MockAuthenticatorCallbacks mock_cb_;
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

    MockAuthenticatorCallbacks mock_cb;
    EXPECT_CALL(mock_cb, onComplete(_)).WillOnce(Invoke([](const Status& status) {
      ASSERT_EQ(status, Status::Ok);
    }));

    auth_->verify(headers, &mock_cb);

    EXPECT_EQ(headers.get_("sec-istio-auth-userinfo"), ExpectedPayloadValue);
    // Verify the token is removed.
    EXPECT_FALSE(headers.Authorization());
  }
}

// This test verifies the Jwt is forwarded if "forward" flag is set.
TEST_F(AuthenticatorTest, TestForwardJwt) {
  // Confirm forward_jwt flag
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_forward(true);
  CreateAuthenticator();
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

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
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));
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
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);
  MockAuthenticatorCallbacks mock_cb;
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  }));

  // Empty headers.
  auto headers = Http::TestHeaderMapImpl{};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies if Jwt is invalid, JwtBadFormat status is returned.
TEST_F(AuthenticatorTest, TestInvalidJWT) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtBadFormat);
  }));

  std::string token = "invalidToken";
  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + token}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies if Authorization header has invalid prefix, JwtMissed status is returned
TEST_F(AuthenticatorTest, TestInvalidPrefix) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer-invalid"}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies when a JWT is non-expiring without audience specified, JwtAudienceNotAllowed
// is returned.
TEST_F(AuthenticatorTest, TestNonExpiringJWT) {
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_, httpAsyncClientForCluster(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtAudienceNotAllowed);
  }));

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(NonExpiringToken)}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies when a JWT is expired, JwtExpired status is returned.
TEST_F(AuthenticatorTest, TestExpiredJWT) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtExpired);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(ExpiredToken)}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies when a JWT is not yet valid, JwtNotYetValid status is returned.
TEST_F(AuthenticatorTest, TestNotYetValidJWT) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtNotYetValid)).Times(1);

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(NotYetValidToken)}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies when an inline JWKS is misconfigured, JwksNoValidKeys is returns
TEST_F(AuthenticatorTest, TestInvalidLocalJwks) {
  auto& provider = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider.clear_remote_jwks();
  provider.mutable_local_jwks()->set_inline_string("invalid");
  CreateAuthenticator();

  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwksNoValidKeys);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies when a JWT is with invalid audience, JwtAudienceNotAllowed is returned.
TEST_F(AuthenticatorTest, TestNonMatchAudJWT) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtAudienceNotAllowed);
  }));

  auto headers =
      Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies when Jwt issuer is not configured, JwtUnknownIssuer is returned.
TEST_F(AuthenticatorTest, TestIssuerNotFound) {
  // Create a config with an other issuer.
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_issuer("other_issuer");
  CreateAuthenticator();

  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtUnknownIssuer);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auth_->verify(headers, &mock_cb_);
}

// This test verifies that when Jwks fetching fails, JwksFetchFail status is returned.
TEST_F(AuthenticatorTest, TestPubkeyFetchFail) {
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .WillOnce(
          Invoke([](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksError(JwksFetcher::JwksReceiver::Failure::InvalidJwks);
          }));

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  }));

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  auth_->verify(headers, &mock_cb_);

  Http::MessagePtr response_message(new Http::ResponseMessageImpl(
      Http::HeaderMapPtr{new Http::TestHeaderMapImpl{{":status", "401"}}}));
}

// This test verifies when a Jwks fetching is not completed yet, but onDestory() is called,
// onComplete() callback should not be called, but internal fetcher_->close() should be called.
// Most importantly, no crash.
TEST_F(AuthenticatorTest, TestOnDestroy) {
  EXPECT_CALL(*fetcher_, fetch(_, _)).Times(1);
  EXPECT_CALL(*fetcher_, cancel()).Times(1);

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
  auto& provider0 = (*proto_config_.mutable_providers())[std::string(ProviderName)];
  provider0.clear_forward_payload_header();
  CreateAuthenticator();
  EXPECT_CALL(*fetcher_, fetch(_, _))
      .WillOnce(Invoke(
          [this](const ::envoy::api::v2::core::HttpUri&, JwksFetcher::JwksReceiver& receiver) {
            receiver.onJwksSuccess(std::move(jwks_));
          }));

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
