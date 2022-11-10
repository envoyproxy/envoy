#include "envoy/extensions/filters/http/jwt_authn/v3/config.pb.h"

#include "source/extensions/filters/http/jwt_authn/filter_config.h"
#include "source/extensions/filters/http/jwt_authn/verifier.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/factory_context.h"
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
  ProviderVerifierTest() {
    mock_factory_ctx_.cluster_manager_.initializeThreadLocalClusters({"pubkey_cluster"});
  }

  void createVerifier() {
    filter_config_ = std::make_unique<FilterConfigImpl>(proto_config_, "", mock_factory_ctx_);
    verifier_ = Verifier::create(proto_config_.rules(0).requires(), proto_config_.providers(),
                                 *filter_config_);
  }

  JwtAuthentication proto_config_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  std::shared_ptr<FilterConfigImpl> filter_config_;
  VerifierConstPtr verifier_;
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

  EXPECT_CALL(mock_cb_, setExtractedData(_))
      .WillOnce(Invoke([](const ProtobufWkt::Struct& payload) {
        EXPECT_TRUE(TestUtility::protoEqual(payload, getExpectedPayload("my_payload")));
      }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));

  auto headers = Http::TestRequestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {"sec-istio-auth-userinfo", ""},
      {"x-jwt-claim-sub", "value-to-be-replaced"},
      {"x-jwt-claim-nested", "header-to-be-deleted"},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_EQ(ExpectedPayloadValue, headers.get_("sec-istio-auth-userinfo"));
  EXPECT_EQ("test@example.com", headers.get_("x-jwt-claim-sub"));
  EXPECT_FALSE(headers.has("x-jwt-claim-nested"));
}

// Test to set the payload (hence dynamic metadata) with the header and payload extracted from the
// verified JWT.
TEST_F(ProviderVerifierTest, TestOkJWTWithExtractedHeaderAndPayload) {
  TestUtility::loadFromYaml(ExampleConfig, proto_config_);
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_payload_in_metadata(
      "my_payload");
  (*proto_config_.mutable_providers())[std::string(ProviderName)].set_header_in_metadata(
      "my_header");
  createVerifier();
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  EXPECT_CALL(mock_cb_, setExtractedData(_))
      .WillOnce(Invoke([](const ProtobufWkt::Struct& payload) {
        // The expected payload is a merged struct of the extracted (from the JWT) payload and
        // header data with "my_payload" and "my_header" as the keys.
        ProtobufWkt::Struct expected_payload;
        MessageUtil::loadFromJson(ExpectedPayloadAndHeaderJSON, expected_payload);
        EXPECT_TRUE(TestUtility::protoEqual(payload, expected_payload));
      }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));

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

  EXPECT_CALL(mock_cb_, setExtractedData(_))
      .WillOnce(Invoke([](const ProtobufWkt::Struct& payload) {
        EXPECT_TRUE(TestUtility::protoEqual(payload, getExpectedPayload("my_payload")));
      }));

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok));

  auto options = Http::AsyncClient::RequestOptions()
                     .setTimeout(std::chrono::milliseconds(5 * 1000))
                     .setParentSpan(parent_span_)
                     .setChildSpanName("JWT Remote PubKey Fetch");
  EXPECT_CALL(mock_factory_ctx_.cluster_manager_.thread_local_cluster_.async_client_,
              send_(_, _, Eq(options)));

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

  EXPECT_CALL(mock_cb_, onComplete(Status::JwtMissed));

  auto headers = Http::TestRequestHeaderMapImpl{
      {"sec-istio-auth-userinfo", ""}, {"x-jwt-claim-sub", ""}, {"x-jwt-claim-nested", ""}};
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_FALSE(headers.has("sec-istio-auth-userinfo"));
  EXPECT_FALSE(headers.has("x-jwt-claim-sub"));
  EXPECT_FALSE(headers.has("x-jwt-claim-nested"));
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
    claim_to_headers:
    - header_name: x-jwt-claim-sub
      claim_name: sub
  other_provider:
    issuer: other_issuer
    forward_payload_header: other-auth-userinfo
    claim_to_headers:
    - header_name: x-jwt-claim-issuer
      claim_name: iss
rules:
- match:
    path: "/"
  requires:
    provider_name: "other_provider"
)";
  TestUtility::loadFromYaml(config, proto_config_);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::JwtUnknownIssuer));

  auto headers = Http::TestRequestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
      {"x-jwt-claim-sub", ""},
      {"x-jwt-claim-issuer", ""},
  };
  context_ = Verifier::createContext(headers, parent_span_, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_TRUE(headers.has("example-auth-userinfo"));
  EXPECT_TRUE(headers.has("x-jwt-claim-sub"));
  EXPECT_FALSE(headers.has("x-jwt-claim-issuer"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// This test verifies that JWT requirement can override audiences
TEST_F(ProviderVerifierTest, TestRequiresProviderWithAudiences) {
  TestUtility::loadFromYaml(ExampleConfig, proto_config_);
  auto* provider_and_audiences =
      proto_config_.mutable_rules(0)->mutable_requires()->mutable_provider_and_audiences();
  provider_and_audiences->set_provider_name("example_provider");
  provider_and_audiences->add_audiences("invalid_service");
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

  EXPECT_THROW(FilterConfigImpl(proto_config_, "", mock_factory_ctx_), EnvoyException);
}

} // namespace
} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
