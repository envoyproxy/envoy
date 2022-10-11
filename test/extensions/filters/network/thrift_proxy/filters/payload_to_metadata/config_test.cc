#include <string>

#include "envoy/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/v3/payload_to_metadata.pb.h"
#include "envoy/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/v3/payload_to_metadata.pb.validate.h"

#include "source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/config.h"
#include "source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/payload_to_metadata_filter.h"

#include "test/extensions/filters/network/thrift_proxy/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace PayloadToMetadataFilter {

using PayloadToMetadataProtoConfig = envoy::extensions::filters::network::thrift_proxy::filters::
    payload_to_metadata::v3::PayloadToMetadata;

void testValidConfig(const std::string& yaml) {
  PayloadToMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);
  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  PayloadToMetadataFilterConfig factory;
  auto cb = factory.createFilterFactoryFromProto(proto_config, "stats", context);
  NetworkFilters::ThriftProxy::ThriftFilters::MockFilterChainFactoryCallbacks filter_callbacks;
  EXPECT_CALL(filter_callbacks, addDecoderFilter(_));
  cb(filter_callbacks);
}

// Tests that a valid config with payload is properly consumed.
TEST(PayloadToMetadataFilterConfigTest, SimpleConfig) {
  const std::string yaml = R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: info
      id: 2
      child:
        name: version
        id: 1
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: default
      value: unknown
  )EOF";

  testValidConfig(yaml);
}

TEST(PayloadToMetadataFilterConfigTest, SimpleConfigWithServiceName) {
  const std::string yaml = R"EOF(
request_rules:
  - service_name: foo
    field_selector:
      name: info
      id: 2
      child:
        name: version
        id: 1
        child:
          name: bar
          id: 100
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: default
      value: unknown
  )EOF";

  testValidConfig(yaml);
}

TEST(PayloadToMetadataFilterConfigTest, SimpleConfigWithServiceNameEndInColon) {
  const std::string yaml = R"EOF(
request_rules:
  - service_name: "foo:"
    field_selector:
      name: info
      id: 2
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: default
      value: unknown
  )EOF";

  testValidConfig(yaml);
}

// does not allow on_missing without value
TEST(PayloadToMetadataFilterConfigTest, MultipleRulesWithSameFieldSelector) {
  std::string yaml =
      R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: info
      id: 2
    on_missing:
      metadata_namespace: envoy.lb
      key: foo
      value: unknown
  - method_name: foo
    field_selector:
      name: info
      id: 2
    on_missing:
      metadata_namespace: envoy.lb
      key: bar
      value: unknown
  )EOF";

  testValidConfig(yaml);
}

// Tests that configuration does not allow value and regex_value_rewrite in the same rule.
TEST(PayloadToMetadataFilterConfigTest, ValueAndRegex) {
  const std::string yaml = R"EOF(
request_rules:
- method_name: foo
  field_selector:
    name: info
    id: 2
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

  PayloadToMetadataProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), EnvoyException);
}

// Both method name and service name are not presented.
TEST(PayloadToMetadataFilterConfigTest, EmptyMethodNameAndServiceName) {
  const std::string yaml = R"EOF(
request_rules:
  - field_selector:
      name: info
      id: 2
      child:
        name: version
        id: 1
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: default
      value: unknown
  )EOF";

  PayloadToMetadataProtoConfig proto_config;
  EXPECT_THROW(TestUtility::loadFromYamlAndValidate(yaml, proto_config), ProtoValidationException);
}

void testForbiddenConfig(const std::string& yaml, const std::string& message) {
  PayloadToMetadataProtoConfig proto_config;
  TestUtility::loadFromYamlAndValidate(yaml, proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  PayloadToMetadataFilterConfig factory;

  EXPECT_THROW_WITH_MESSAGE(factory.createFilterFactoryFromProto(proto_config, "stats", context),
                            EnvoyException, message);
}

// does not allow rule without either on_present or on_missing
TEST(PayloadToMetadataFilterConfigTest, EmptyOnPresentOnMissing) {
  std::string yaml =
      R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: info
      id: 2
  )EOF";
  std::string error_message =
      "payload to metadata filter: neither `on_present` nor `on_missing` set";

  testForbiddenConfig(yaml, error_message);
}

// does not allow on_missing without value
TEST(PayloadToMetadataFilterConfigTest, NoValueInOnMissing) {
  std::string yaml =
      R"EOF(
request_rules:
  - method_name: foo
    field_selector:
      name: info
      id: 2
    on_missing:
      metadata_namespace: envoy.lb
      key: foo
      type: STRING
  )EOF";
  std::string error_message =
      "payload to metadata filter: cannot specify on_missing rule without non-empty value";

  testForbiddenConfig(yaml, error_message);
}

} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
