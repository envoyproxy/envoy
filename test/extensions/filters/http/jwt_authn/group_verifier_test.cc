#include "extensions/filters/http/jwt_authn/verifier.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::google::jwt_verify::Status;
using ::testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

class GroupVerifierTest : public ::testing::Test {
public:
  void createVerifier() {
    ON_CALL(mock_factory_, create(_, _, _))
        .WillByDefault(
            Invoke([&](const std::vector<std::string>&, const absl::optional<std::string>&, bool) {
              return std::move(mock_auths_[idx++]);
            }));
    verifier_ = Verifier::create(proto_config_.rules()[0].requires(), proto_config_.providers(),
                                 mock_factory_, NiceMock<MockExtractor>());
  }
  JwtAuthentication proto_config_;
  VerifierPtr verifier_;
  MockVerifierCallbacks mock_cb_;
  std::vector<std::unique_ptr<MockAuthenticator>> mock_auths_;
  int idx = 0;
  NiceMock<MockAuthFactory> mock_factory_;
  ContextSharedPtr context_;
};

// Deeply nested anys that ends in provider name
TEST_F(GroupVerifierTest, DeeplyNestedAnys) {
  const char config[] = R"(
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
    forward_payload_header: sec-istio-auth-userinfo
    from_params:
    - jwta
    - jwtb
    - jwtc
rules:
- match: { path: "/match" }
  requires:
    requires_any:
      requirements:
      - requires_any:
          requirements:
          - requires_any:
              requirements:
              - provider_name: "example_provider"
)";
  MessageUtil::loadFromYaml(config, proto_config_);
  createVerifier();

  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
      .WillOnce(Invoke([](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                          AuthenticatorCallback callback) { callback(Status::Ok); }));
  mock_auths_.push_back(std::move(mock_auth));

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {":path", "/match?jwta=" + std::string(GoodToken) + "&jwtb=" + std::string(ExpiredToken)},
      {"sec-istio-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("sec-istio-auth-userinfo"));
}

// require alls that just ends
TEST_F(GroupVerifierTest, CanHandleUnexpectedEnd) {
  const char config[] = R"(
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
    forward_payload_header: sec-istio-auth-userinfo
rules:
- match: { path: "/match" }
  requires:
    requires_all:
      requirements:
      - requires_all:
)";
  MessageUtil::loadFromYaml(config, proto_config_);
  createVerifier();

  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _)).Times(0);
  mock_auths_.push_back(std::move(mock_auth));

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{{":path", "/match"}};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(GroupVerifierTest, TestRequiresAll) {
  MessageUtil::loadFromYaml(RequiresAllConfig, proto_config_);

  for (int i = 0; i < 2; ++i) {
    auto mock_auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
        .WillRepeatedly(Invoke([](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                                  AuthenticatorCallback callback) { callback(Status::Ok); }));
    mock_auths_.push_back(std::move(mock_auth));
  }
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {":path",
       "/requires-all?jwt_a=" + std::string(GoodToken) + "&jwt_b=" + std::string(OtherGoodToken)},
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// test requires all with first token returning bad format
TEST_F(GroupVerifierTest, TestRequiresAllBadFormat) {
  MessageUtil::loadFromYaml(RequiresAllConfig, proto_config_);
  std::vector<AuthenticatorCallback> callbacks;
  for (int i = 0; i < 2; ++i) {
    auto mock_auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
        .WillRepeatedly(Invoke(
            [&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                AuthenticatorCallback callback) { callbacks.push_back(std::move(callback)); }));
    mock_auths_.push_back(std::move(mock_auth));
  }
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtBadFormat);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {":path", "/requires-all?jwt_a=xxx&jwt_b=xxx"},
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks[0](Status::JwtBadFormat);
  // can keep invoking callback
  callbacks[1](Status::Ok);
  callbacks[0](Status::Ok);
  callbacks[1](Status::Ok);

  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// test requires all with second token returning missing jwt
TEST_F(GroupVerifierTest, TestRequiresAllMissing) {
  MessageUtil::loadFromYaml(RequiresAllConfig, proto_config_);
  std::vector<AuthenticatorCallback> callbacks;
  for (int i = 0; i < 2; ++i) {
    auto mock_auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
        .WillRepeatedly(Invoke(
            [&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                AuthenticatorCallback callback) { callbacks.push_back(std::move(callback)); }));
    mock_auths_.push_back(std::move(mock_auth));
  }
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtMissed);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {":path", "/requires-all?jwt_a=xxx&jwt_b=xxx"},
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks[0](Status::Ok);
  callbacks[1](Status::JwtMissed);
  // can keep invoking callback
  callbacks[0](Status::Ok);
  callbacks[1](Status::Ok);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

TEST_F(GroupVerifierTest, TestRequiresAllWrongLocations) {
  MessageUtil::loadFromYaml(RequiresAllConfig, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
      .WillRepeatedly(
          Invoke([](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                    AuthenticatorCallback callback) { callback(Status::JwtUnknownIssuer); }));
  mock_auths_.push_back(std::move(mock_auth));
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtUnknownIssuer);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {":path",
       "/requires-all?jwt_a=" + std::string(OtherGoodToken) + "&jwt_b=" + std::string(GoodToken)},
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_TRUE(headers.has("other-auth-userinfo"));
}

TEST_F(GroupVerifierTest, TestRequiresAny) {
  MessageUtil::loadFromYaml(RequiresAnyConfig, proto_config_);
  for (int i = 0; i < 4; ++i) {
    auto mock_auth = std::make_unique<MockAuthenticator>();
    if (i % 2 == 0) {
      EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
          .WillRepeatedly(Invoke([](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                                    AuthenticatorCallback callback) { callback(Status::Ok); }));
    } else {
      EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
          .WillRepeatedly(
              Invoke([](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                        AuthenticatorCallback callback) { callback(Status::JwtUnknownIssuer); }));
    }
    mock_auths_.push_back(std::move(mock_auth));
  }
  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
      .WillRepeatedly(
          Invoke([](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                    AuthenticatorCallback callback) { callback(Status::JwtUnknownIssuer); }));
  mock_auths_.push_back(std::move(mock_auth));
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(_))
      .Times(3)
      .WillOnce(Invoke([](const Status& status) { ASSERT_EQ(status, Status::Ok); }))
      .WillOnce(Invoke([](const Status& status) { ASSERT_EQ(status, Status::Ok); }))
      .WillOnce(Invoke([](const Status& status) { ASSERT_EQ(status, Status::JwtUnknownIssuer); }));
  auto headers = Http::TestHeaderMapImpl{
      {"a", "Bearer " + std::string(GoodToken)},
      {"b", "Bearer " + std::string(InvalidAudToken)},
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
      {":path", "/requires-any"},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_TRUE(headers.has("other-auth-userinfo"));

  headers = Http::TestHeaderMapImpl{
      {"a", "Bearer " + std::string(InvalidAudToken)},
      {"b", "Bearer " + std::string(OtherGoodToken)},
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
      {":path", "/requires-any"},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));

  headers = Http::TestHeaderMapImpl{
      {"a", "Bearer " + std::string(InvalidAudToken)},
      {"b", "Bearer " + std::string(InvalidAudToken)},
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
      {":path", "/requires-any"},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
