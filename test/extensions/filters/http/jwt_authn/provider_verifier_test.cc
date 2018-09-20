#include "extensions/filters/http/jwt_authn/filter_config.h"
#include "extensions/filters/http/jwt_authn/verifier.h"

#include "test/extensions/filters/http/jwt_authn/mock.h"
#include "test/extensions/filters/http/jwt_authn/test_common.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"

using ::envoy::config::filter::http::jwt_authn::v2alpha::JwtAuthentication;
using ::google::jwt_verify::Status;
using ::testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JwtAuthn {

class ProviderVerifierTest : public ::testing::Test {
public:
  void createVerifier() {
    filter_config_ = ::std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_);
    verifier_ = Verifier::create(proto_config_.rules(0).requires(), proto_config_.providers(),
                                 *filter_config_, filter_config_->getExtractor());
  }

  JwtAuthentication proto_config_;
  FilterConfigSharedPtr filter_config_;
  VerifierPtr verifier_;
  NiceMock<Server::Configuration::MockFactoryContext> mock_factory_ctx_;
  ContextSharedPtr context_;
  MockVerifierCallbacks mock_cb_;
};

TEST_F(ProviderVerifierTest, TestOkJWT) {
  MessageUtil::loadFromYaml(ExampleConfig, proto_config_);
  createVerifier();
  MockUpstream mock_pubkey(mock_factory_ctx_.cluster_manager_, PublicKey);

  EXPECT_CALL(mock_cb_, onComplete(Status::Ok)).Times(1);

  auto headers = Http::TestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {"sec-istio-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_EQ(ExpectedPayloadValue, headers.get_("sec-istio-auth-userinfo"));
}

TEST_F(ProviderVerifierTest, TestMissedJWT) {
  MessageUtil::loadFromYaml(ExampleConfig, proto_config_);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::JwtMissed)).Times(1);

  auto headers = Http::TestHeaderMapImpl{{"sec-istio-auth-userinfo", ""}};
  context_ = Verifier::createContext(headers, &mock_cb_);
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
  MessageUtil::loadFromYaml(config, proto_config_);
  createVerifier();

  EXPECT_CALL(mock_cb_, onComplete(Status::JwtUnknownIssuer)).Times(1);

  auto headers = Http::TestHeaderMapImpl{
      {"Authorization", "Bearer " + std::string(GoodToken)},
      {"example-auth-userinfo", ""},
      {"other-auth-userinfo", ""},
  };
  context_ = Verifier::createContext(headers, &mock_cb_);
  verifier_->verify(context_);
  EXPECT_TRUE(headers.has("example-auth-userinfo"));
  EXPECT_FALSE(headers.has("other-auth-userinfo"));
}

// This test verifies that JWT requirement can override audiences
TEST_F(ProviderVerifierTest, TestRequiresProviderWithAudiences) {
  MessageUtil::loadFromYaml(ExampleConfig, proto_config_);
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

  auto headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(GoodToken)}};
  verifier_->verify(Verifier::createContext(headers, &mock_cb_));
  headers = Http::TestHeaderMapImpl{{"Authorization", "Bearer " + std::string(InvalidAudToken)}};
  verifier_->verify(Verifier::createContext(headers, &mock_cb_));
}

// This test verifies that requirement referencing nonexistent provider will throw exception
TEST_F(ProviderVerifierTest, TestRequiresNonexistentProvider) {
  MessageUtil::loadFromYaml(ExampleConfig, proto_config_);
  proto_config_.mutable_rules(0)->mutable_requires()->set_provider_name("nosuchprovider");

  EXPECT_THROW(::std::make_shared<FilterConfig>(proto_config_, "", mock_factory_ctx_),
               EnvoyException);
}

} // namespace JwtAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
