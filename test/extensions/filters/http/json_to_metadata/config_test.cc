#include "source/extensions/filters/http/json_to_metadata/config.h"
#include "source/extensions/filters/http/json_to_metadata/filter.h"

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

TEST(Factory, NoRuleInRouteConfig) {
  const std::string yaml_empty = R"({})";

  JsonToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_empty, *proto_config);
  NiceMock<Server::Configuration::MockServerFactoryContext> context;
  auto status = factory
                    .createRouteSpecificFilterConfig(*proto_config, context,
                                                     ProtobufMessage::getNullValidationVisitor())
                    .status();
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.message(),
            "json_to_metadata_filter: Per route configs must at least specify one of request_rules "
            "or response_rules.");
}

TEST(Factory, PerRouteConfig) {
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
  )";

  JsonToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_request, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, context,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_TRUE(config->doRequest());
  EXPECT_FALSE(config->doResponse());
}

TEST(Factory, PerRouteConfigWithResponseRules) {
  const std::string yaml_response = R"(
response_rules:
  rules:
  - selectors:
    - key: version
    on_present:
      metadata_namespace: envoy.lb
      key: version
  )";

  JsonToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_response, *proto_config);

  NiceMock<Server::Configuration::MockServerFactoryContext> context;

  const auto route_config =
      factory
          .createRouteSpecificFilterConfig(*proto_config, context,
                                           ProtobufMessage::getNullValidationVisitor())
          .value();
  const auto* config = dynamic_cast<const FilterConfig*>(route_config.get());
  EXPECT_FALSE(config->doRequest());
  EXPECT_TRUE(config->doResponse());
}

} // namespace JsonToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
