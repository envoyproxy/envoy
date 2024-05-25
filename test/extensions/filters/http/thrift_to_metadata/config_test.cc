#include "source/extensions/filters/http/thrift_to_metadata/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ThriftToMetadata {

TEST(Factory, Basic) {
  const std::string yaml = R"(
request_rules:
- field: PROTOCOL
  on_present:
    metadata_namespace: envoy.lb
    key: protocol
  on_missing:
    metadata_namespace: envoy.lb
    key: protocol
    value: "unknown"
- field: TRANSPORT
  on_present:
    metadata_namespace: envoy.lb
    key: transport
  on_missing:
    metadata_namespace: envoy.lb
    key: transport
    value: "unknown"
response_rules:
- field: MESSAGE_TYPE
  on_present:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_message_type
  on_missing:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_message_type
    value: "exception"
- field: REPLY_TYPE
  on_present:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_reply_type
  on_missing:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_reply_type
    value: "error"
  )";

  ThriftToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;

  auto callback = factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  callback(filter_callback);
}

TEST(Factory, NoOnPresentOnMissing) {
  const std::string yaml_request = R"(
request_rules:
- field: PROTOCOL
  )";

  const std::string yaml_response = R"(
response_rules:
- field: PROTOCOL
  )";

  ThriftToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_request, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "thrift to metadata filter: neither `on_present` nor `on_missing` set");
  TestUtility::loadFromYaml(yaml_response, *proto_config);
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "thrift to metadata filter: neither `on_present` nor `on_missing` set");
}

TEST(Factory, NoValueIntOnMissing) {
  const std::string yaml_request = R"(
request_rules:
- field: PROTOCOL
  on_present:
    metadata_namespace: envoy.lb
    key: protocol
  on_missing:
    metadata_namespace: envoy.lb
    key: protocol
  )";

  const std::string yaml_response = R"(
response_rules:
- field: MESSAGE_TYPE
  on_present:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_message_type
  on_missing:
    metadata_namespace: envoy.filters.http.thrift_to_metadata
    key: response_message_type
  )";

  ThriftToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_request, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "thrift to metadata filter: cannot specify on_missing rule with empty value");
  TestUtility::loadFromYaml(yaml_response, *proto_config);
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException, "thrift to metadata filter: cannot specify on_missing rule with empty value");
}

TEST(Factory, NoRule) {
  const std::string yaml_empty = R"({})";

  ThriftToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml_empty, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException,
      "thrift_to_metadata filter: Per filter configs must at least specify either request or "
      "response rules");
}

TEST(Factory, NoTwitterProtocol) {
  const std::string yaml = R"(
request_rules:
- field: PROTOCOL
  on_present:
    metadata_namespace: envoy.lb
    key: protocol
protocol: TWITTER
  )";

  ThriftToMetadataConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);
  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).status().IgnoreError(),
      EnvoyException);
}

} // namespace ThriftToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
