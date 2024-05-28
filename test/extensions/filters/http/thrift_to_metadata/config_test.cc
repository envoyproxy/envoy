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

} // namespace ThriftToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
