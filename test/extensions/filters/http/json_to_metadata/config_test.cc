#include "source/extensions/filters/http/json_to_metadata/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace JsonToMetadata {

TEST(Factory, Basic) {
  const std::string yaml_request = R"(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: version
      value: 'unknown'
      preserve_existing_metadata_value: true
    on_error:
      metadata_namespace: envoy.lb
      key: version
      value: 'error'
      preserve_existing_metadata_value: true
  )";

  const std::string yaml_response = R"(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: version
      value: 'unknown'
      preserve_existing_metadata_value: true
    on_error:
      metadata_namespace: envoy.lb
      key: version
      value: 'error'
      preserve_existing_metadata_value: true
  )";

  JsonToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_request, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto callback = factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  callback(filter_callback);

  TestUtility::loadFromYaml(yaml_response, *proto_config);
  callback = factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  callback(filter_callback);
}

TEST(Factory, NoOnPresentOnMissing) {
  const std::string yaml_request = R"(
request_rules:
  rules:
  - selectors:
    - key: version
  )";

  const std::string yaml_response = R"(
response_rules:
  rules:
  - selectors:
    - key: version
  )";

  JsonToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_request, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "json to metadata filter: neither `on_present` nor `on_missing` set");
  TestUtility::loadFromYaml(yaml_response, *proto_config);
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "json to metadata filter: neither `on_present` nor `on_missing` set");
}

TEST(Factory, NoValueIntOnMissing) {
  const std::string yaml_request = R"(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: version
  )";

  const std::string yaml_response = R"(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_missing:
      metadata_namespace: envoy.lb
      key: version
  )";

  JsonToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_request, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "json to metadata filter: cannot specify on_missing rule with empty value");
  TestUtility::loadFromYaml(yaml_response, *proto_config);
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "json to metadata filter: cannot specify on_missing rule with empty value");
}

TEST(Factory, NoValueIntOnError) {
  const std::string yaml_request = R"(
request_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_error:
      metadata_namespace: envoy.lb
      key: version
  )";

  const std::string yaml_response = R"(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
    on_error:
      metadata_namespace: envoy.lb
      key: version
  )";

  JsonToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_request, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "json to metadata filter: cannot specify on_error rule with empty value");
  TestUtility::loadFromYaml(yaml_response, *proto_config);
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "json to metadata filter: cannot specify on_error rule with empty value");
}

TEST(Factory, NoRule) {
  const std::string yaml_empty = R"({})";

  JsonToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_empty, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "json_to_metadata_filter: Per filter configs must at least specify");
}

} // namespace JsonToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
