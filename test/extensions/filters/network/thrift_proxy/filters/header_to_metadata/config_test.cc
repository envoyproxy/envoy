#include <string>

#include "envoy/extensions/filters/network/thrift_proxy/filters/header_to_metadata/v3/header_to_metadata.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/filters/header_to_metadata/v3/header_to_metadata.pb.validate.h"

#include "source/extensions/filters/network/thrift_proxy/filters/header_to_metadata/config.h"
#include "source/extensions/filters/network/thrift_proxy/filters/header_to_metadata/header_to_metadata_filter.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace HeaderToMetadataFilter {

using HeaderToMetadataProtoConfig = envoy::extensions::filters::network::thrift_proxy::filters::
    header_to_metadata::v3::HeaderToMetadata;

void testForbiddenConfig(const std::string& yaml, const std::string& message) {
  HeaderToMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  HeaderToMetadataFilterConfig factory;

  EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(proto_config, "stats", context),
                            EnvoyException, message);
}

// Tests that empty (metadata) keys are rejected.
TEST(HeaderToMetadataFilterConfigTest, InvalidEmptyKey) {
  const std::string yaml = R"EOF(
request_rules:
  - header: x-version
    on_present:
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
    on_present:
      metadata_namespace: envoy.lb
      key: version
      type: STRING
    on_missing:
      metadata_namespace: envoy.lb
      key: default
      value: 'true'
      type: STRING
  )EOF";

  HeaderToMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  HeaderToMetadataFilterConfig factory;

  auto cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  NetworkFilters::ThriftProxy::ThriftFilters::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addDecoderFilter(_));
  cb(filter_callbacks);
}

// Tests that configuration does not allow value and regex_value_rewrite in the same rule.
TEST(HeaderToMetadataFilterConfigTest, ValueAndRegex) {
  const std::string yaml = R"EOF(
request_rules:
  - header: x-version
    on_present:
      metadata_namespace: envoy.lb
      key: cluster
      value: foo
      regex_value_rewrite:
        pattern:
          google_re2: {}
          regex: "^/(cluster[\\d\\w-]+)/?.*$"
        substitution: "\\1"
  )EOF";

  HeaderToMetadataProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

// Tests that configuration does not allow rule without either on_present or on_missing.
TEST(HeaderToMetadataFilterConfigTest, InvalidEmptyRule) {
  const std::string yaml = R"EOF(
request_rules:
  - header: x-no-exist
  )EOF";

  testForbiddenConfig(yaml, "header to metadata filter: rule for header 'x-no-exist' has neither "
                            "`on_present` nor `on_missing` set");
}

// Tests that on_missing rules don't allow an empty value.
TEST(HeaderToMetadataFilterConfigTest, OnHeaderMissingEmptyValue) {
  const std::string yaml = R"EOF(
request_rules:
  - header: x-version
    on_missing:
      metadata_namespace: envoy.lb
      key: "foo"
      type: STRING
  )EOF";

  testForbiddenConfig(yaml, "Cannot specify on_missing rule with empty value");
}

} // namespace HeaderToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
