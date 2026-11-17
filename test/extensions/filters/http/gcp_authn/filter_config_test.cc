#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.h"
#include "envoy/extensions/filters/http/gcp_authn/v3/gcp_authn.pb.validate.h"
#include "envoy/type/v3/percent.pb.h"

#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/http/gcp_authn/filter_config.h"

#include "test/mocks/server/factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace GcpAuthn {
namespace {

using ::envoy::extensions::filters::http::gcp_authn::v3::GcpAuthnFilterConfig;

TEST(GcpAuthnFilterConfigTest, DEPRECATED_FEATURE_TEST(GcpAuthnFilterWithCorrectProto)) {
  std::string filter_config_yaml = R"EOF(
    http_uri:
      uri: http://test/path
      cluster: test_cluster
      timeout:
        seconds: 5
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 32s
      num_retries: 5
  )EOF";
  GcpAuthnFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GcpAuthnFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(GcpAuthnFilterConfigTest, GcpAuthnFilterWithNewProto) {
  std::string filter_config_yaml = R"EOF(
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 32s
      num_retries: 5
    cluster: test_cluster
    timeout:
        seconds: 5
  )EOF";
  GcpAuthnFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GcpAuthnFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(GcpAuthnFilterConfigTest, GcpAuthnFilterWithTokenMetadataKey) {
  std::string filter_config_yaml = R"EOF(
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 32s
      num_retries: 5
    cluster: test_cluster
    timeout:
        seconds: 5
    token_metadata_key: token_key
  )EOF";
  GcpAuthnFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GcpAuthnFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(GcpAuthnFilterConfigTest, GcpAuthnFilterWithAudience) {
  std::string filter_config_yaml = R"EOF(
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 32s
      num_retries: 5
    cluster: test_cluster
    timeout:
        seconds: 5
    audience:
      url: http://config_audience
  )EOF";
  GcpAuthnFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GcpAuthnFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(GcpAuthnFilterConfigTest, GcpAuthnFilterWithIamAccessToken) {
  std::string filter_config_yaml = R"EOF(
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 32s
      num_retries: 5
    cluster: test_cluster
    timeout:
        seconds: 5
    audience:
      iam_access_token:
        account: "sa@proj.iam.gserviceaccount.com"
        authorization: "Bearer token"
  )EOF";
  GcpAuthnFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GcpAuthnFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(GcpAuthnFilterConfigTest, GcpAuthnFilterWithInvalidIamAccessTokenFormatter) {
  std::string filter_config_yaml = R"EOF(
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 32s
      num_retries: 5
    cluster: test_cluster
    timeout:
        seconds: 5
    audience:
      iam_access_token:
        account: "%INVALID_FORMATTER("
        authorization: "Bearer token"
  )EOF";
  GcpAuthnFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GcpAuthnFilterFactory factory;
  auto result = factory.createFilterFactoryFromProto(filter_config, "stats", context);
  EXPECT_FALSE(result.ok());
}

TEST(GcpAuthnFilterConfigTest, GcpAuthnFilterWithPreserveExistingHeader) {
  std::string filter_config_yaml = R"EOF(
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 32s
      num_retries: 5
    cluster: test_cluster
    timeout:
        seconds: 5
    token_header:
      name: Authorization
      value_prefix: "Bearer "
      preserve_existing: {}
  )EOF";
  GcpAuthnFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GcpAuthnFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(GcpAuthnFilterConfigTest, GcpAuthnFilterWithPreserveExistingMissingName) {
  std::string filter_config_yaml = R"EOF(
    retry_policy:
      retry_back_off:
        base_interval: 0.1s
        max_interval: 32s
      num_retries: 5
    cluster: test_cluster
    timeout:
        seconds: 5
    token_header:
      preserve_existing: {}
  )EOF";
  GcpAuthnFilterConfig filter_config;
  TestUtility::loadFromYaml(filter_config_yaml, filter_config);
  TestUtility::validate(filter_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  GcpAuthnFilterFactory factory;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(filter_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamDecoderFilter(_));
  cb(filter_callback);
}

TEST(GcpAuthnFilterConfigTest, AudienceScopesValidation) {
  envoy::extensions::filters::http::gcp_authn::v3::Audience audience;
  audience.mutable_access_token()->add_scopes("https://www.googleapis.com/auth/cloud-platform");
  audience.mutable_access_token()->add_scopes("openid");
  EXPECT_NO_THROW(TestUtility::validate(audience));

  // Empty scope should fail validation
  audience.mutable_access_token()->clear_scopes();
  audience.mutable_access_token()->add_scopes("");
  EXPECT_THROW(TestUtility::validate(audience), ProtoValidationException);

  // Scope with comma should fail validation
  audience.mutable_access_token()->clear_scopes();
  audience.mutable_access_token()->add_scopes("scope1,scope2");
  EXPECT_THROW(TestUtility::validate(audience), ProtoValidationException);

  // Scope with space should fail validation
  audience.mutable_access_token()->clear_scopes();
  audience.mutable_access_token()->add_scopes("scope 1");
  EXPECT_THROW(TestUtility::validate(audience), ProtoValidationException);

  // Valid bound_access_token scopes
  envoy::extensions::filters::http::gcp_authn::v3::Audience bound_audience;
  bound_audience.mutable_bound_access_token()->add_scopes(
      "https://www.googleapis.com/auth/cloud-platform");
  EXPECT_NO_THROW(TestUtility::validate(bound_audience));

  // Empty bound_access_token scope should fail validation
  bound_audience.mutable_bound_access_token()->clear_scopes();
  bound_audience.mutable_bound_access_token()->add_scopes("");
  EXPECT_THROW(TestUtility::validate(bound_audience), ProtoValidationException);

  // Scope with comma in bound_access_token should fail validation
  bound_audience.mutable_bound_access_token()->clear_scopes();
  bound_audience.mutable_bound_access_token()->add_scopes("scope1,scope2");
  EXPECT_THROW(TestUtility::validate(bound_audience), ProtoValidationException);

  // Scope with space in bound_access_token should fail validation
  bound_audience.mutable_bound_access_token()->clear_scopes();
  bound_audience.mutable_bound_access_token()->add_scopes("scope 1");
  EXPECT_THROW(TestUtility::validate(bound_audience), ProtoValidationException);
}

} // namespace
} // namespace GcpAuthn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
