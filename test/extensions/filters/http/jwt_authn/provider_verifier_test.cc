#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "extensions/filters/http/jwt_authn/filter_config.h"
#include "extensions/filters/http/jwt_authn/verifier.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using envoy::extensions::filters::http::jwt_authn::v3::JwtAuthentication;
using ::google::jwt_verify::Status;
using ::testing::Eq;
using ::testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {
namespace {

ProtobufWkt::Struct getExpectedPayload(const std::string& name) {
  ProtobufWkt::Struct expected_payload;
  TestUtility::loadFromJson(ExpectedPayloadJSON, expected_payload);

  ProtobufWkt::Struct struct_obj;
  *(*struct_obj.mutable_fields())[name].mutable_struct_value() = expected_payload;
  return struct_obj;
}

class ProviderVerifierTest : public testing::Test {
public:
  void createVerifier() {
    filter_config_ = FilterConfigImpl::create(proto_config_, "", mock_factory_ctx_);
    verifier_ = Verifier::create(proto_config_.rules(0).requires(), proto_config_.providers(),
                                 *filter_config_);
  }

  JwtAuthentication proto_config_;
  std::shared_ptr<FilterConfigImpl> filter_config_;
  VerifierConstPtr verifier_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  ContextSharedPtr context_;
  MockVerifierCallbacks mock_cb_;
  NiceMock<Tracing::MockSpan> parent_span_;
};

TEST_F(ProviderVerifierTest, TestOkJWT) {
  TestUtility::loadFromYaml(ExampleConfig, proto_config_);
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_payload_in_metadata(
      "my_payload");
  createVerifier();
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  EXPECT_CALL(mock_cb_, setPayload(_)).WillOnce(Invoke([](const ProtobufWkt::Struct& payload) {
    EXPECT_TRUE(TestUtility::protoEqual(payload, getExpectedPayload("my_payload")));
  }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);

  auto headers = Http::TestRequestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {"sec-istio-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_EQ(ExpectedPayloadValue, headers.get_("sec-istio-auth-userinfo"));
}

TEST_F(ProviderVerifierTest, TestSpanPassedDown) {
  TestUtility::loadFromYaml(ExampleConfig, proto_config_);
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_payload_in_metadata(
      "my_payload");
  createVerifier();
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  EXPECT_CALL(mock_cb_, setPayload(_)).WillOnce(Invoke([](const ProtobufWkt::Struct& payload) {
    EXPECT_TRUE(TestUtility::protoEqual(payload, getExpectedPayload("my_payload")));
  }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);

  auto options = Http::AsyncClient::RequestOptions()
                     .setTimeout(std::chrono::milliseconds(5 * 1000))
                     .setParentSpan(parent_span_)
                     .setChildSpanName("JWT Remote PubKey Fetch");
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_.async_client_, send_(_, _, Eq(options))).Times(1);

  auto headers = Http::TestRequestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {"sec-istio-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
}

TEST_F(ProviderVerifierTest, TestMissedJWT) {
  TestUtility::loadFromYaml(ExampleConfig, proto_config_);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::JwtMissed)).Times(1);

  auto headers = Http::TestRequestHeaderMapImpl{{"sec-istio-auth-userinfo", ""}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("sec-istio-auth-userinfo"));
}

// This test verifies that JWT must be issued by the provider specified in the requirement.
TEST_F(ProviderVerifierTest, TestTokenRequirementProviderMismatch) {
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
    forward_payload_header: example-auth-userinfo
  other_provider:
    issuer: other_issuer
    forward_payload_header: other-auth-userinfo
rules:
- match:
    path: "/"
  requires:
    provider_name: "other_provider"
)";
  TestUtility::loadFromYaml(config, proto_config_);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::JwtUnknownIssuer)).Times(1);

  auto headers = Http::TestRequestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_TRUE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// This test verifies that JWT requirement can override audiences
TEST_F(ProviderVerifierTest, TestRequiresProviderWithAudiences) {
  TestUtility::loadFromYaml(ExampleConfig, proto_config_);
  auto* requires =
      proto_config_.mutable_rules(0)->mutable_requires()->mutable_provider_and_audiences();
  requires->set_provider_name("example_provider");
  requires->add_audiences("invalid_service");
  createVerifier();
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  EXPECT_CALL(mock_cb_, onComplete(_))
      .WillOnce(
          Invoke([](const Status& status) { ASSERT_EQ(status, Status::JwtAudienceNotAllowed); }))
      .WillOnce(Invoke([](const Status& status) { ASSERT_EQ(status, Status::Ok); }));

  auto headers =
      Http::TestRequestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  verifier_->verify(Verifier::createContext(headers, parent_span_, &mock_cb_));
  headers =
      Http::TestRequestHeaderMapImpl{{"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  verifier_->verify(Verifier::createContext(headers, parent_span_, &mock_cb_));
}

// This test verifies that requirement referencing nonexistent provider will throw exception
TEST_F(ProviderVerifierTest, TestRequiresNonexistentProvider) {
  TestUtility::loadFromYaml(ExampleConfig, proto_config_);
  proto_config_.mutable_rules(0)->mutable_requires()->set_provider_name("nosuchprovider");

  EXPECT_THROW(FilterConfigImpl::create(proto_config_, "", mock_factory_ctx_), EnvoyException);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
