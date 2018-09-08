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
const char AllWithAny[] = R"(
providers:
  provider_1:
  provider_2:
  provider_3:
rules:
- match: { path: "/" }
  requires:
    requires_all:
      requirements:
      - requires_any:
          requirements:
            - provider_name: "provider_1"
            - provider_name: "provider_2"
      - provider_name: "provider_3"
)";

const char AnyWithAll[] = R"(
providers:
  provider_1:
  provider_2:
  provider_3:
  provider_4:
rules:
- match: { path: "/" }
  requires:
    requires_any:
      requirements:
      - requires_all:
          requirements:
            - provider_name: "provider_1"
            - provider_name: "provider_2"
      - requires_all:
          requirements:
            - provider_name: "provider_3"
            - provider_name: "provider_4"
)";

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
  auto headers = Http::TestHeaderMapImpl{};
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
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// test requires all with first auth returning bad format
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

// test requires all with second auth returning missing jwt
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
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_TRUE(headers.has("other-auth-userinfo"));
}

TEST_F(GroupVerifierTest, TestRequiresAnyFirstAuthOK) {
  MessageUtil::loadFromYaml(RequiresAnyConfig, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
      .WillRepeatedly(Invoke([](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                                AuthenticatorCallback callback) { callback(Status::Ok); }));
  mock_auths_.push_back(std::move(mock_auth));
  createVerifier();
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_TRUE(headers.has("other-auth-userinfo"));
}

TEST_F(GroupVerifierTest, TestRequiresAnyLastAuthOk) {
  MessageUtil::loadFromYaml(RequiresAnyConfig, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
      .WillRepeatedly(
          Invoke([](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                    AuthenticatorCallback callback) { callback(Status::JwtUnknownIssuer); }));
  mock_auths_.push_back(std::move(mock_auth));
  mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
      .WillRepeatedly(Invoke([](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                                AuthenticatorCallback callback) { callback(Status::Ok); }));
  mock_auths_.push_back(std::move(mock_auth));
  createVerifier();
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

TEST_F(GroupVerifierTest, TestRequiresAnyAllAuthFailed) {
  MessageUtil::loadFromYaml(RequiresAnyConfig, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
      .WillRepeatedly(
          Invoke([](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                    AuthenticatorCallback callback) { callback(Status::JwtHeaderBadKid); }));
  mock_auths_.push_back(std::move(mock_auth));
  mock_auth = std::make_unique<MockAuthenticator>();
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
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates first require any is OK and proivder_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllFirstAnyIsOk) {
  MessageUtil::loadFromYaml(AllWithAny, proto_config_);
  for (int i = 0; i < 2; ++i) {
    auto mock_auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
        .WillRepeatedly(Invoke([&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                                   AuthenticatorCallback callback) { callback(Status::Ok); }));
    mock_auths_.push_back(std::move(mock_auth));
  }
  createVerifier();
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates first require any is OK and proivder_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllLastAnyIsOk) {
  MessageUtil::loadFromYaml(AllWithAny, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
      .WillRepeatedly(
          Invoke([&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                     AuthenticatorCallback callback) { callback(Status::JwtUnknownIssuer); }));
  mock_auths_.push_back(std::move(mock_auth));
  for (int i = 0; i < 2; ++i) {
    mock_auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
        .WillRepeatedly(Invoke([&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                                   AuthenticatorCallback callback) { callback(Status::Ok); }));
    mock_auths_.push_back(std::move(mock_auth));
  }
  createVerifier();
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates all require any OK and proivder_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllBothInRequireAnyIsOk) {
  MessageUtil::loadFromYaml(AllWithAny, proto_config_);
  std::vector<AuthenticatorCallback> callbacks;
  for (int i = 0; i < 3; ++i) {
    auto mock_auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
        .WillRepeatedly(Invoke(
            [&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                AuthenticatorCallback callback) { callbacks.push_back(std::move(callback)); }));
    mock_auths_.push_back(std::move(mock_auth));
  }
  createVerifier();
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  for (const auto callback : callbacks) {
    callback(Status::Ok);
  }
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates all require any failed and proivder_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllBothInRequireAnyFailed) {
  MessageUtil::loadFromYaml(AllWithAny, proto_config_);
  std::vector<AuthenticatorCallback> callbacks;
  for (int i = 0; i < 3; ++i) {
    auto mock_auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
        .WillRepeatedly(Invoke(
            [&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                AuthenticatorCallback callback) { callbacks.push_back(std::move(callback)); }));
    mock_auths_.push_back(std::move(mock_auth));
  }
  createVerifier();
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwksFetchFail);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks[0](Status::JwksFetchFail);
  callbacks[1](Status::JwksFetchFail);
  callbacks[2](Status::Ok);
}

TEST_F(GroupVerifierTest, TestAllInAnyBothRequireAllFailed) {
  MessageUtil::loadFromYaml(AnyWithAll, proto_config_);
  std::vector<AuthenticatorCallback> callbacks;
  for (int i = 0; i < 4; ++i) {
    auto mock_auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
        .WillRepeatedly(Invoke(
            [&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                AuthenticatorCallback callback) { callbacks.push_back(std::move(callback)); }));
    mock_auths_.push_back(std::move(mock_auth));
  }
  createVerifier();
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::JwtExpired);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks[2](Status::JwksFetchFail);
  callbacks[1](Status::JwtExpired);
}

TEST_F(GroupVerifierTest, TestAllInAnyFirstAllIsOk) {
  MessageUtil::loadFromYaml(AnyWithAll, proto_config_);
  std::vector<AuthenticatorCallback> callbacks;
  for (int i = 0; i < 4; ++i) {
    auto mock_auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
        .WillRepeatedly(Invoke(
            [&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                AuthenticatorCallback callback) { callbacks.push_back(std::move(callback)); }));
    mock_auths_.push_back(std::move(mock_auth));
  }
  createVerifier();
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks[1](Status::Ok);
  callbacks[0](Status::Ok);
}

TEST_F(GroupVerifierTest, TestAllInAnyLastAllIsOk) {
  MessageUtil::loadFromYaml(AnyWithAll, proto_config_);
  std::vector<AuthenticatorCallback> callbacks;
  for (int i = 0; i < 4; ++i) {
    auto mock_auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
        .WillRepeatedly(Invoke(
            [&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                AuthenticatorCallback callback) { callbacks.push_back(std::move(callback)); }));
    mock_auths_.push_back(std::move(mock_auth));
  }
  createVerifier();
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks[2](Status::Ok);
  callbacks[3](Status::Ok);
  callbacks[1](Status::JwtExpired);
}

TEST_F(GroupVerifierTest, TestAllInAnyBothRequireAllAreOk) {
  MessageUtil::loadFromYaml(AnyWithAll, proto_config_);
  for (int i = 0; i < 4; ++i) {
    auto mock_auth = std::make_unique<MockAuthenticator>();
    EXPECT_CALL(*mock_auth.get(), doVerify(_, _, _))
        .WillRepeatedly(Invoke([&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                                   AuthenticatorCallback callback) { callback(Status::Ok); }));
    mock_auths_.push_back(std::move(mock_auth));
  }
  createVerifier();
  EXPECT_CALL(mock_cb_, onComplete(_)).WillOnce(Invoke([](const Status& status) {
    ASSERT_EQ(status, Status::Ok);
  }));
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
