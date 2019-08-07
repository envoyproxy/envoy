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
    issuer: iss_1
  provider_2:
    issuer: iss_2
  provider_3:
    issuer: iss_3
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
    issuer: iss_1
  provider_2:
    issuer: iss_2
  provider_3:
    issuer: iss_3
  provider_4:
    issuer: iss_4
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

using StatusMap = std::unordered_map<std::string, const Status&>;

constexpr auto allowfailed = "_allow_failed_";

class GroupVerifierTest : public testing::Test {
public:
  void createVerifier() {
    ON_CALL(mock_factory_, create(_, _, _))
        .WillByDefault(Invoke([&](const ::google::jwt_verify::CheckAudience*,
                                  const absl::optional<std::string>& provider, bool) {
          return std::move(mock_auths_[provider ? provider.value() : allowfailed]);
        }));
    verifier_ = Verifier::create(proto_config_.rules(0).requires(), proto_config_.providers(),
                                 mock_factory_, mock_extractor_);
    ON_CALL(mock_extractor_, extract(_)).WillByDefault(Invoke([](const Http::HeaderMap&) {
      return std::vector<JwtLocationConstPtr>{};
    }));
  }
  void createSyncMockAuthsAndVerifier(const StatusMap& statuses) {
    for (const auto& it : statuses) {
      auto mock_auth = std::make_unique<MockAuthenticator>();
      EXPECT_CALL(*mock_auth, doVerify(_, _, _, _))
          .WillOnce(Invoke([issuer = it.first, status = it.second](
                               Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                               SetPayloadCallback set_payload_cb, AuthenticatorCallback callback) {
            if (status == Status::Ok) {
              ProtobufWkt::Struct empty_struct;
              set_payload_cb(issuer, empty_struct);
            }
            callback(status);
          }));
      EXPECT_CALL(*mock_auth, onDestroy()).Times(1);
      mock_auths_[it.first] = std::move(mock_auth);
    }
    createVerifier();
  }

  // This expected payload is only for createSyncMockAuthsAndVerifier() function
  // which set an empty payload struct for each issuer.
  static ProtobufWkt::Struct getExpectedPayload(const std::vector<std::string>& issuers) {
    ProtobufWkt::Struct struct_obj;
    auto* fields = struct_obj.mutable_fields();
    for (const auto& issuer : issuers) {
      ProtobufWkt::Struct empty_struct;
      *(*fields)[issuer].mutable_struct_value() = empty_struct;
    }
    return struct_obj;
  }

  std::unordered_map<std::string, AuthenticatorCallback>
  createAsyncMockAuthsAndVerifier(const std::vector<std::string>& providers) {
    std::unordered_map<std::string, AuthenticatorCallback> callbacks;
    for (const auto& provider : providers) {
      auto mock_auth = std::make_unique<MockAuthenticator>();
      EXPECT_CALL(*mock_auth, doVerify(_, _, _, _))
          .WillOnce(Invoke(
              [&callbacks, iss = provider](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*,
                                           SetPayloadCallback, AuthenticatorCallback callback) {
                callbacks[iss] = std::move(callback);
              }));
      EXPECT_CALL(*mock_auth, onDestroy()).Times(1);
      mock_auths_[provider] = std::move(mock_auth);
    }
    createVerifier();
    return callbacks;
  }

  JwtAuthentication proto_config_;
  VerifierConstPtr verifier_;
  MockVerifierCallbacks mock_cb_;
  std::unordered_map<std::string, std::unique_ptr<MockAuthenticator>> mock_auths_;
  NiceMock<MockAuthFactory> mock_factory_;
  ContextSharedPtr context_;
  NiceMock<MockExtractor> mock_extractor_;
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
  TestUtility::loadFromYaml(config, proto_config_);
  createSyncMockAuthsAndVerifier(StatusMap{{"example_provider", Status::Ok}});

  EXPECT_CALL(mock_cb_, setPayload(_)).WillOnce(Invoke([](const ProtobufWkt::Struct& payload) {
    EXPECT_TRUE(TestUtility::protoEqual(payload, getExpectedPayload({"example_provider"})));
  }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
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
  TestUtility::loadFromYaml(config, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth, doVerify(_, _, _, _)).Times(0);
  mock_auths_["example_provider"] = std::move(mock_auth);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

// test requires all with both auth returning OK
TEST_F(GroupVerifierTest, TestRequiresAll) {
  TestUtility::loadFromYaml(RequiresAllConfig, proto_config_);
  createSyncMockAuthsAndVerifier(
      StatusMap{{"example_provider", Status::Ok}, {"other_provider", Status::Ok}});

  EXPECT_CALL(mock_cb_, setPayload(_)).WillOnce(Invoke([](const ProtobufWkt::Struct& payload) {
    EXPECT_TRUE(TestUtility::protoEqual(
        payload, getExpectedPayload({"example_provider", "other_provider"})));
  }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
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
  TestUtility::loadFromYaml(RequiresAllConfig, proto_config_);
  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"example_provider", "other_provider"});

  // onComplete with failure status, not payload
  EXPECT_CALL(mock_cb_, setPayload(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtBadFormat)).Times(1);
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["example_provider"](Status::JwtBadFormat);
  // can keep invoking callback
  callbacks["other_provider"](Status::Ok);
  callbacks["example_provider"](Status::Ok);
  callbacks["other_provider"](Status::Ok);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// test requires all with second auth returning missing jwt
TEST_F(GroupVerifierTest, TestRequiresAllMissing) {
  TestUtility::loadFromYaml(RequiresAllConfig, proto_config_);
  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"example_provider", "other_provider"});

  // onComplete with failure status, not payload
  EXPECT_CALL(mock_cb_, setPayload(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtMissed)).Times(1);
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["example_provider"](Status::Ok);
  callbacks["other_provider"](Status::JwtMissed);
  // can keep invoking callback
  callbacks["example_provider"](Status::Ok);
  callbacks["other_provider"](Status::Ok);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// Test requires all and mock auths simulate cache misses and async return of failure statuses.
TEST_F(GroupVerifierTest, TestRequiresAllBothFailed) {
  TestUtility::loadFromYaml(RequiresAllConfig, proto_config_);
  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"example_provider", "other_provider"});

  // onComplete with failure status, not payload
  EXPECT_CALL(mock_cb_, setPayload(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtUnknownIssuer)).Times(1);
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
  callbacks["example_provider"](Status::JwtUnknownIssuer);
  callbacks["other_provider"](Status::JwtUnknownIssuer);
}

// Test requires any with first auth returning OK.
TEST_F(GroupVerifierTest, TestRequiresAnyFirstAuthOK) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  createSyncMockAuthsAndVerifier(StatusMap{{"example_provider", Status::Ok}});

  EXPECT_CALL(mock_cb_, setPayload(_)).WillOnce(Invoke([](const ProtobufWkt::Struct& payload) {
    EXPECT_TRUE(TestUtility::protoEqual(payload, getExpectedPayload({"example_provider"})));
  }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_TRUE(headers.has("other-auth-userinfo"));
}

// Test requires any with last auth returning OK.
TEST_F(GroupVerifierTest, TestRequiresAnyLastAuthOk) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  createSyncMockAuthsAndVerifier(
      StatusMap{{"example_provider", Status::JwtUnknownIssuer}, {"other_provider", Status::Ok}});

  EXPECT_CALL(mock_cb_, setPayload(_)).WillOnce(Invoke([](const ProtobufWkt::Struct& payload) {
    EXPECT_TRUE(TestUtility::protoEqual(payload, getExpectedPayload({"other_provider"})));
  }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// Test requires any with both auth returning error. Requires any returns the error last received
// back to the caller.
TEST_F(GroupVerifierTest, TestRequiresAnyAllAuthFailed) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  createSyncMockAuthsAndVerifier(StatusMap{{"example_provider", Status::JwtHeaderBadKid},
                                           {"other_provider", Status::JwtUnknownIssuer}});

  // onComplete with failure status, not payload
  EXPECT_CALL(mock_cb_, setPayload(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtUnknownIssuer)).Times(1);
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
// Test simulates first require any is OK and provider_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllFirstAnyIsOk) {
  TestUtility::loadFromYaml(AllWithAny, proto_config_);
  createSyncMockAuthsAndVerifier(StatusMap{{"provider_1", Status::Ok}, {"provider_3", Status::Ok}});

  EXPECT_CALL(mock_cb_, setPayload(_)).WillOnce(Invoke([](const ProtobufWkt::Struct& payload) {
    EXPECT_TRUE(TestUtility::protoEqual(payload, getExpectedPayload({"provider_1", "provider_3"})));
  }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates first require any is OK and provider_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllLastAnyIsOk) {
  TestUtility::loadFromYaml(AllWithAny, proto_config_);
  createSyncMockAuthsAndVerifier(StatusMap{{"provider_1", Status::JwtUnknownIssuer},
                                           {"provider_2", Status::Ok},
                                           {"provider_3", Status::Ok}});

  EXPECT_CALL(mock_cb_, setPayload(_)).WillOnce(Invoke([](const ProtobufWkt::Struct& payload) {
    EXPECT_TRUE(TestUtility::protoEqual(payload, getExpectedPayload({"provider_2", "provider_3"})));
  }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates all require any OK and provider_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllBothInRequireAnyIsOk) {
  TestUtility::loadFromYaml(AllWithAny, proto_config_);
  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"provider_1", "provider_2", "provider_3"});

  // AsyncMockVerifier doesn't set payload
  EXPECT_CALL(mock_cb_, setPayload(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["provider_1"](Status::Ok);
  callbacks["provider_2"](Status::Ok);
  callbacks["provider_3"](Status::Ok);
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates all require any failed and provider_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllBothInRequireAnyFailed) {
  TestUtility::loadFromYaml(AllWithAny, proto_config_);
  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"provider_1", "provider_2", "provider_3"});

  EXPECT_CALL(mock_cb_, setPayload(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwksFetchFail)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["provider_1"](Status::JwksFetchFail);
  callbacks["provider_2"](Status::JwksFetchFail);
  callbacks["provider_3"](Status::Ok);
}

// Test contains a requires any which in turn has 2 requires all. Mock auths simulate JWKs cache
// hits and inline return of errors. Requires any returns the error last received back to the
// caller.
TEST_F(GroupVerifierTest, TestAllInAnyBothRequireAllFailed) {
  TestUtility::loadFromYaml(AnyWithAll, proto_config_);
  createSyncMockAuthsAndVerifier(
      StatusMap{{"provider_1", Status::JwksFetchFail}, {"provider_3", Status::JwtExpired}});

  EXPECT_CALL(mock_cb_, setPayload(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtExpired)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
}

// Test contains a requires any which in turn has 2 requires all. The first inner requires all is
// completed with OKs. Mock auths simulate JWKs cache misses and async return of OKs.
TEST_F(GroupVerifierTest, TestAllInAnyFirstAllIsOk) {
  TestUtility::loadFromYaml(AnyWithAll, proto_config_);
  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"provider_1", "provider_2", "provider_3", "provider_4"});

  // AsyncMockVerifier doesn't set payload
  EXPECT_CALL(mock_cb_, setPayload(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["provider_2"](Status::Ok);
  callbacks["provider_3"](Status::JwtMissed);
  callbacks["provider_1"](Status::Ok);
}

// Test contains a requires any which in turn has 2 requires all. The last inner requires all is
// completed with OKs. Mock auths simulate JWKs cache misses and async return of OKs.
TEST_F(GroupVerifierTest, TestAllInAnyLastAllIsOk) {
  TestUtility::loadFromYaml(AnyWithAll, proto_config_);
  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"provider_1", "provider_2", "provider_3", "provider_4"});

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["provider_3"](Status::Ok);
  callbacks["provider_4"](Status::Ok);
  callbacks["provider_2"](Status::JwtExpired);
}

// Test contains a requires any which in turn has 2 requires all. The both inner requires all are
// completed with OKs. Mock auths simulate JWKs cache misses and async return of OKs.
TEST_F(GroupVerifierTest, TestAllInAnyBothRequireAllAreOk) {
  TestUtility::loadFromYaml(AnyWithAll, proto_config_);
  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"provider_1", "provider_2", "provider_3", "provider_4"});

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);
  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks["provider_1"](Status::Ok);
  callbacks["provider_2"](Status::Ok);
  callbacks["provider_3"](Status::Ok);
  callbacks["provider_4"](Status::Ok);
}

// Test require any with additional allow all
TEST_F(GroupVerifierTest, TestRequiresAnyWithAllowAll) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  proto_config_.mutable_rules(0)
      ->mutable_requires()
      ->mutable_requires_any()
      ->add_requirements()
      ->mutable_allow_missing_or_failed();

  auto callbacks = createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"example_provider", "other_provider"});
  auto mock_auth = std::make_unique<MockAuthenticator>();
  EXPECT_CALL(*mock_auth, doVerify(_, _, _, _))
      .WillOnce(Invoke(
          [&](Http::HeaderMap&, std::vector<JwtLocationConstPtr>*, SetPayloadCallback,
              AuthenticatorCallback callback) { callbacks[allowfailed] = std::move(callback); }));
  EXPECT_CALL(*mock_auth, onDestroy()).Times(1);
  mock_auths_[allowfailed] = std::move(mock_auth);
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);

  auto headers = Http::TestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  callbacks[allowfailed](Status::Ok);
  // with requires any, if any inner verifier returns OK the whole any verifier should return OK.
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
