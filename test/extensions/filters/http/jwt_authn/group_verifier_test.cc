#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/extensions/filters/http/jwt_authn/verifier.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
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

using StatusMap = absl::node_hash_map<std::string, const Status>;

constexpr auto allowfailed = "_allow_failed_";

class GroupVerifierTest : public testing::Test {
public:
  void createVerifier() {
    ON_CALL(mock_factory_, create(_, _, _, _))
        .WillByDefault(Invoke([&](const ::google::jwt_verify::CheckAudience*,
                                  const absl::optional<std::string>& provider, bool, bool) {
          return std::move(mock_auths_[provider ? provider.value() : allowfailed]);
        }));
    verifier_ = Verifier::create(proto_config_.rules(0).requires_(), proto_config_.providers(),
                                 mock_factory_);
  }
  void createSyncMockAuthsAndVerifier(const StatusMap& statuses) {
    for (const auto& it : statuses) {
      auto mock_auth = std::make_unique<MockAuthenticator>();
      EXPECT_CALL(*mock_auth, doVerify(_, _, _, _, _))
          .WillOnce(
              Invoke([issuer = it.first, status = it.second](
                         Http::RequestHeaderMap&, Tracing::Span&, std::vector<JwtLocationConstPtr>*,
                         SetExtractedJwtDataCallback set_extracted_jwt_data_cb,
                         AuthenticatorCallback callback) {
                if (status == Status::Ok) {
                  ProtobufWkt::Struct empty_struct;
                  set_extracted_jwt_data_cb(issuer, empty_struct);
                }
                callback(status);
              }));
      EXPECT_CALL(*mock_auth, onDestroy());
      mock_auths_[it.first] = std::move(mock_auth);
    }
    createVerifier();
  }

  // This expected extracted data is only for createSyncMockAuthsAndVerifier() function
  // which set an empty extracted data struct for each issuer.
  static ProtobufWkt::Struct getExpectedExtractedData(const std::vector<std::string>& issuers) {
    ProtobufWkt::Struct struct_obj;
    auto* fields = struct_obj.mutable_fields();
    for (const auto& issuer : issuers) {
      ProtobufWkt::Struct empty_struct;
      *(*fields)[issuer].mutable_struct_value() = empty_struct;
    }
    return struct_obj;
  }

  void createAsyncMockAuthsAndVerifier(const std::vector<std::string>& providers) {
    for (const auto& provider : providers) {
      auto mock_auth = std::make_unique<MockAuthenticator>();
      EXPECT_CALL(*mock_auth, doVerify(_, _, _, _, _))
          .WillOnce(Invoke([&, iss = provider](Http::RequestHeaderMap&, Tracing::Span&,
                                               std::vector<JwtLocationConstPtr>*,
                                               SetExtractedJwtDataCallback,
                                               AuthenticatorCallback callback) {
            callbacks_[iss] = std::move(callback);
          }));
      EXPECT_CALL(*mock_auth, onDestroy());
      mock_auths_[provider] = std::move(mock_auth);
    }
    createVerifier();
  }

  JwtAuthentication proto_config_;
  VerifierConstPtr verifier_;
  MockVerifierCallbacks mock_cb_;
  absl::node_hash_map<std::string, std::unique_ptr<MockAuthenticator>> mock_auths_;
  NiceMock<MockAuthFactory> mock_factory_;
  ContextSharedPtr context_;
  NiceMock<Tracing::MockSpan> parent_span_;
  absl::node_hash_map<std::string, AuthenticatorCallback> callbacks_;
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
    claim_to_headers:
    - header_name: x-jwt-claim-aud
      claim_name: aud
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

  EXPECT_CALL(mock_cb_, setExtractedData(_))
      .WillOnce(Invoke([](const ProtobufWkt::Struct& extracted_data) {
        EXPECT_TRUE(TestUtility::protoEqual(extracted_data,
                                            getExpectedExtractedData({"example_provider"})));
      }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{
      {"sec-istio-auth-userinfo", ""},
      {"x-jwt-claim-aud", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("sec-istio-auth-userinfo"));
  EXPECT_FALSE(headers.has("x-jwt-claim-aud"));
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
  EXPECT_CALL(*mock_auth, doVerify(_, _, _, _, _)).Times(0);
  mock_auths_["example_provider"] = std::move(mock_auth);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

// test requires all with both auth returning OK
TEST_F(GroupVerifierTest, TestRequiresAll) {
  TestUtility::loadFromYaml(RequiresAllConfig, proto_config_);
  createSyncMockAuthsAndVerifier(
      StatusMap{{"example_provider", Status::Ok}, {"other_provider", Status::Ok}});

  EXPECT_CALL(mock_cb_, setExtractedData(_))
      .WillOnce(Invoke([](const ProtobufWkt::Struct& extracted_data) {
        EXPECT_TRUE(TestUtility::protoEqual(
            extracted_data, getExpectedExtractedData({"example_provider", "other_provider"})));
      }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// test requires all with first auth returning bad format
TEST_F(GroupVerifierTest, TestRequiresAllBadFormat) {
  TestUtility::loadFromYaml(RequiresAllConfig, proto_config_);
  createAsyncMockAuthsAndVerifier(std::vector<std::string>{"example_provider", "other_provider"});

  // onComplete with a failure status, no extracted data.
  EXPECT_CALL(mock_cb_, setExtractedData(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtBadFormat));
  auto headers = Http::TestRequestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  callbacks_["example_provider"](Status::JwtBadFormat);
  // can keep invoking callback
  callbacks_["other_provider"](Status::Ok);
  callbacks_["example_provider"](Status::Ok);
  callbacks_["other_provider"](Status::Ok);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// test requires all with second auth returning missing jwt
TEST_F(GroupVerifierTest, TestRequiresAllMissing) {
  TestUtility::loadFromYaml(RequiresAllConfig, proto_config_);
  createAsyncMockAuthsAndVerifier(std::vector<std::string>{"example_provider", "other_provider"});

  // onComplete with a failure status, no extracted data.
  EXPECT_CALL(mock_cb_, setExtractedData(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtMissed));
  auto headers = Http::TestRequestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  callbacks_["example_provider"](Status::Ok);
  callbacks_["other_provider"](Status::JwtMissed);
  // can keep invoking callback
  callbacks_["example_provider"](Status::Ok);
  callbacks_["other_provider"](Status::Ok);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// Test requires all and mock auths simulate cache misses and async return of failure statuses.
TEST_F(GroupVerifierTest, TestRequiresAllBothFailed) {
  TestUtility::loadFromYaml(RequiresAllConfig, proto_config_);
  createAsyncMockAuthsAndVerifier(std::vector<std::string>{"example_provider", "other_provider"});

  // onComplete with a failure status, no extracted data.
  EXPECT_CALL(mock_cb_, setExtractedData(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtUnknownIssuer));
  auto headers = Http::TestRequestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
  callbacks_["example_provider"](Status::JwtUnknownIssuer);
  callbacks_["other_provider"](Status::JwtUnknownIssuer);
}

// Test requires any with first auth returning OK.
TEST_F(GroupVerifierTest, TestRequiresAnyFirstAuthOK) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  createSyncMockAuthsAndVerifier(StatusMap{{"example_provider", Status::Ok}});

  EXPECT_CALL(mock_cb_, setExtractedData(_))
      .WillOnce(Invoke([](const ProtobufWkt::Struct& extracted_data) {
        EXPECT_TRUE(TestUtility::protoEqual(extracted_data,
                                            getExpectedExtractedData({"example_provider"})));
      }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_TRUE(headers.has("other-auth-userinfo"));
}

// Test requires any with last auth returning OK.
TEST_F(GroupVerifierTest, TestRequiresAnyLastAuthOk) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  createSyncMockAuthsAndVerifier(
      StatusMap{{"example_provider", Status::JwtUnknownIssuer}, {"other_provider", Status::Ok}});

  EXPECT_CALL(mock_cb_, setExtractedData(_))
      .WillOnce(Invoke([](const ProtobufWkt::Struct& extracted_data) {
        EXPECT_TRUE(
            TestUtility::protoEqual(extracted_data, getExpectedExtractedData({"other_provider"})));
      }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// Test requires any with both auth returning error. Requires any returns the error last received
// back to the caller.
TEST_F(GroupVerifierTest, TestRequiresAnyAllAuthFailed) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  createSyncMockAuthsAndVerifier(StatusMap{{"example_provider", Status::JwtMissed},
                                           {"other_provider", Status::JwtHeaderBadKid}});

  // onComplete with a failure status, no extracted data.
  EXPECT_CALL(mock_cb_, setExtractedData(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtHeaderBadKid));
  auto headers = Http::TestRequestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// Test requires any with both auth returning errors, last error is JwtMissed.
// Usually the final error is from the last one.
// But if a token is not for a provider, that provider auth will either return
// JwtMissed or JwtUnknownIssuer, such error should not be used for the final
// error in Any case
TEST_F(GroupVerifierTest, TestRequiresAnyLastIsJwtMissed) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  createSyncMockAuthsAndVerifier(StatusMap{{"example_provider", Status::JwtHeaderBadKid},
                                           {"other_provider", Status::JwtMissed}});

  // onComplete with a failure status, no extracted data.
  EXPECT_CALL(mock_cb_, setExtractedData(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtHeaderBadKid));
  auto headers = Http::TestRequestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// Test requires any with both auth returning errors: last error is
// JwtUnknownIssuer
TEST_F(GroupVerifierTest, TestRequiresAnyLastIsJwtUnknownIssuer) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  auto mock_auth = std::make_unique<MockAuthenticator>();
  createSyncMockAuthsAndVerifier(StatusMap{{"example_provider", Status::JwtHeaderBadKid},
                                           {"other_provider", Status::JwtUnknownIssuer}});

  // onComplete with a failure status, no extracted data.
  EXPECT_CALL(mock_cb_, setExtractedData(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtHeaderBadKid));
  auto headers = Http::TestRequestHeaderMapImpl{
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates first require any is OK and provider_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllFirstAnyIsOk) {
  TestUtility::loadFromYaml(AllWithAny, proto_config_);
  createSyncMockAuthsAndVerifier(StatusMap{{"provider_1", Status::Ok}, {"provider_3", Status::Ok}});

  EXPECT_CALL(mock_cb_, setExtractedData(_))
      .WillOnce(Invoke([](const ProtobufWkt::Struct& extracted_data) {
        EXPECT_TRUE(TestUtility::protoEqual(
            extracted_data, getExpectedExtractedData({"provider_1", "provider_3"})));
      }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates first require any is OK and provider_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllLastAnyIsOk) {
  TestUtility::loadFromYaml(AllWithAny, proto_config_);
  createSyncMockAuthsAndVerifier(StatusMap{{"provider_1", Status::JwtUnknownIssuer},
                                           {"provider_2", Status::Ok},
                                           {"provider_3", Status::Ok}});

  EXPECT_CALL(mock_cb_, setExtractedData(_))
      .WillOnce(Invoke([](const ProtobufWkt::Struct& extracted_data) {
        EXPECT_TRUE(TestUtility::protoEqual(
            extracted_data, getExpectedExtractedData({"provider_2", "provider_3"})));
      }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates all require any OK and provider_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllBothInRequireAnyIsOk) {
  TestUtility::loadFromYaml(AllWithAny, proto_config_);
  createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"provider_1", "provider_2", "provider_3"});

  // AsyncMockVerifier doesn't set the extracted data.
  EXPECT_CALL(mock_cb_, setExtractedData(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  callbacks_["provider_1"](Status::Ok);
  callbacks_["provider_2"](Status::Ok);
  callbacks_["provider_3"](Status::Ok);
}

// Test contains a 2 provider_name in a require any along with another provider_name in require all.
// Test simulates all require any failed and provider_name is OK.
TEST_F(GroupVerifierTest, TestAnyInAllBothInRequireAnyFailed) {
  TestUtility::loadFromYaml(AllWithAny, proto_config_);
  createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"provider_1", "provider_2", "provider_3"});

  EXPECT_CALL(mock_cb_, setExtractedData(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwksFetchFail));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  callbacks_["provider_1"](Status::JwksFetchFail);
  callbacks_["provider_2"](Status::JwksFetchFail);
  callbacks_["provider_3"](Status::Ok);
}

// Test contains a requires any which in turn has 2 requires all. Mock auths simulate JWKs cache
// hits and inline return of errors. Requires any returns the error last received back to the
// caller.
TEST_F(GroupVerifierTest, TestAllInAnyBothRequireAllFailed) {
  TestUtility::loadFromYaml(AnyWithAll, proto_config_);
  createSyncMockAuthsAndVerifier(
      StatusMap{{"provider_1", Status::JwksFetchFail}, {"provider_3", Status::JwtExpired}});

  EXPECT_CALL(mock_cb_, setExtractedData(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtExpired));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

// Test contains a requires any which in turn has 2 requires all. The first inner requires all is
// completed with OKs. Mock auths simulate JWKs cache misses and async return of OKs.
TEST_F(GroupVerifierTest, TestAllInAnyFirstAllIsOk) {
  TestUtility::loadFromYaml(AnyWithAll, proto_config_);
  createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"provider_1", "provider_2", "provider_3", "provider_4"});

  // AsyncMockVerifier doesn't set the extracted data.
  EXPECT_CALL(mock_cb_, setExtractedData(_)).Times(0);
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  callbacks_["provider_2"](Status::Ok);
  callbacks_["provider_3"](Status::JwtMissed);
  callbacks_["provider_1"](Status::Ok);
}

// Test contains a requires any which in turn has 2 requires all. The last inner requires all is
// completed with OKs. Mock auths simulate JWKs cache misses and async return of OKs.
TEST_F(GroupVerifierTest, TestAllInAnyLastAllIsOk) {
  TestUtility::loadFromYaml(AnyWithAll, proto_config_);
  createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"provider_1", "provider_2", "provider_3", "provider_4"});

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  callbacks_["provider_3"](Status::Ok);
  callbacks_["provider_4"](Status::Ok);
  callbacks_["provider_2"](Status::JwtExpired);
}

// Test contains a requires any which in turn has 2 requires all. The both inner requires all are
// completed with OKs. Mock auths simulate JWKs cache misses and async return of OKs.
TEST_F(GroupVerifierTest, TestAllInAnyBothRequireAllAreOk) {
  TestUtility::loadFromYaml(AnyWithAll, proto_config_);
  createAsyncMockAuthsAndVerifier(
      std::vector<std::string>{"provider_1", "provider_2", "provider_3", "provider_4"});

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));
  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  callbacks_["provider_1"](Status::Ok);
  callbacks_["provider_2"](Status::Ok);
  callbacks_["provider_3"](Status::Ok);
  callbacks_["provider_4"](Status::Ok);
}

// Test RequiresAny with two providers and allow_failed
TEST_F(GroupVerifierTest, TestRequiresAnyWithAllowFailed) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  proto_config_.mutable_rules(0)
      ->mutable_requires_()
      ->mutable_requires_any()
      ->add_requirements()
      ->mutable_allow_missing_or_failed();

  createAsyncMockAuthsAndVerifier(std::vector<std::string>{"example_provider", "other_provider"});
  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));

  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  callbacks_["example_provider"](Status::JwtMissed);
  callbacks_["other_provider"](Status::JwtExpired);
}

// Test RequiresAny with two providers and allow_missing, failed
TEST_F(GroupVerifierTest, TestRequiresAnyWithAllowMissingButFailed) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  proto_config_.mutable_rules(0)
      ->mutable_requires_()
      ->mutable_requires_any()
      ->add_requirements()
      ->mutable_allow_missing();

  createAsyncMockAuthsAndVerifier(std::vector<std::string>{"example_provider", "other_provider"});
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtExpired));

  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  callbacks_["example_provider"](Status::JwtMissed);
  callbacks_["other_provider"](Status::JwtExpired);
}

// Test RequiresAny with two providers and allow_missing, but one returns JwtUnknownIssuer
TEST_F(GroupVerifierTest, TestRequiresAnyWithAllowMissingButUnknownIssuer) {
  TestUtility::loadFromYaml(RequiresAnyConfig, proto_config_);
  proto_config_.mutable_rules(0)
      ->mutable_requires_()
      ->mutable_requires_any()
      ->add_requirements()
      ->mutable_allow_missing();

  createAsyncMockAuthsAndVerifier(std::vector<std::string>{"example_provider", "other_provider"});
  EXPECT_CALL(mock_cb_, onComplete(Status::JwtUnknownIssuer));

  auto headers = Http::TestRequestHeaderMapImpl{};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  callbacks_["example_provider"](Status::JwtMissed);
  callbacks_["other_provider"](Status::JwtUnknownIssuer);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
