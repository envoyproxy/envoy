#include <string>

#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.h"
#include "envoy/extensions/filters/http/header_to_metadata/v3/header_to_metadata.pb.validate.h"

#include "source/extensions/filters/http/header_to_metadata/config.h"
#include "source/extensions/filters/http/header_to_metadata/header_to_metadata_filter.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace HeaderToMetadataFilter {

using HeaderToMetadataProtoConfig =
    envoy::extensions::filters::http::header_to_metadata::v3::Config;

void testForbiddenConfig(const std::string& yaml) {
  HeaderToMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  HeaderToMetadataConfig factory;

  EXPECT_THROW(factory.createFilterFactoryFromProto(proto_config, "stats", context),
               EnvoyException);
}

// Tests that empty (metadata) keys are rejected.
TEST(HeaderToMetadataFilterConfigTest, InvalidEmptyKey) {
  const std::string yaml = R"EOF(
request_rules:
  - header: x-version
    on_header_present:
      metadata_namespace: envoy.lb
      key: ""
      type: STRING
  )EOF";

  HeaderToMetadataProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException);
}

// Tests that empty (metadata) keys are rejected in case of cookie.
TEST(HeaderToMetadataFilterConfigTest, InvalidEmptyCookieKey) {
  const std::string yaml = R"EOF(
request_rules:
  - cookie: x-cookie
    on_header_present:
      metadata_namespace: envoy.lb
      key: ""
      type: STRING
  )EOF";

  HeaderToMetadataProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException);
}

// Tests that a valid config with header is properly consumed.
TEST(HeaderToMetadataFilterConfigTest, SimpleConfig) {
  const std::string yaml = R"EOF(
request_rules:
  - header: x-version
    on_header_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
    on_header_missing:
      metadata_namespace: envoy.lb
      key: default
      value: 'true'
      type: STRING
  )EOF";

  HeaderToMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  HeaderToMetadataConfig factory;

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

// Tests that a valid config with cookie is properly consumed.
TEST(HeaderToMetadataFilterConfigTest, SimpleCookieConfig) {
  const std::string yaml = R"EOF(
request_rules:
  - cookie: x-cookie
    on_header_present:
      metadata_namespace: envoy.lb
      key: version1
      type: STRING
    on_header_missing:
      metadata_namespace: envoy.lb
      key: default
      value: 'true'
      type: STRING
  )EOF";

  HeaderToMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  HeaderToMetadataConfig factory;

  Http::FilterFactoryCb cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addStreamFilter(_));
  cb(filter_callbacks);
}

// Tests that per route config properly overrides the global config.
TEST(HeaderToMetadataFilterConfigTest, PerRouteConfig) {
  const std::string yaml = R"EOF(
request_rules:
  - header: x-version
    on_header_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
    on_header_missing:
      metadata_namespace: envoy.lb
      key: default
      value: 'true'
      type: STRING
  )EOF";

  HeaderToMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  HeaderToMetadataConfig factory;

  const auto route_config = factory.createRouteSpecificFilterConfig(
      proto_config, context, ProtobufMessage::getNullValidationVisitor());
  const auto* config = dynamic_cast<const Config*>(route_config.get());
  EXPECT_TRUE(config->doRequest());
  EXPECT_FALSE(config->doResponse());
}

// Tests that configuration does not allow value and regex_value_rewrite in the same rule.
TEST(HeaderToMetadataFilterConfigTest, ValueAndRegex) {
  const std::string yaml = R"EOF(
request_rules:
  - header: x-version
    on_header_present:
      metadata_namespace: envoy.lb
      key: cluster
      value: foo
      regex_value_rewrite:
        pattern:
          regex: "^/(cluster[\\d\\w-]+)/?.*$"
        substitution: "\\1"
  )EOF";

  testForbiddenConfig(yaml);
}

// Tests that cookie configuration does not allow value and regex_value_rewrite in the same rule.
TEST(HeaderToMetadataFilterConfigTest, CookieValueAndRegex) {
  const std::string yaml = R"EOF(
request_rules:
  - cookie: x-cookie
    on_header_present:
      metadata_namespace: envoy.lb
      key: cluster
      value: foo
      regex_value_rewrite:
        pattern:
          regex: "^/(cluster[\\d\\w-]+)/?.*$"
        substitution: "\\1"
  )EOF";

  testForbiddenConfig(yaml);
}

// Tests that on_header_missing rules don't allow an empty value.
TEST(HeaderToMetadataFilterConfigTest, OnHeaderMissingEmptyValue) {
  const std::string yaml = R"EOF(
request_rules:
  - header: x-version
    on_header_missing:
      metadata_namespace: envoy.lb
      key: "foo"
      type: STRING
  )EOF";

  testForbiddenConfig(yaml);
}

// Tests that on_header_missing rules don't allow an empty cookie value.
TEST(HeaderToMetadataFilterConfigTest, CookieOnHeaderMissingEmptyValue) {
  const std::string yaml = R"EOF(
request_rules:
  - cookie: x-cookie
    on_header_missing:
      metadata_namespace: envoy.lb
      key: "foo"
      type: STRING
  )EOF";

  testForbiddenConfig(yaml);
}

} // namespace HeaderToMetadataFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
