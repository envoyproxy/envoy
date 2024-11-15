#include "source/extensions/filters/http/ext_proc/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ExternalProcessing {
namespace {

TEST(HttpExtProcConfigTest, CorrectConfig) {
  std::string yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_proc_server
      stat_prefix: google
  failure_mode_allow: true
  request_attributes:
  - 'Foo'
  - 'Bar'
  - 'Baz'
  response_attributes:
  - 'More'
  processing_mode:
    request_header_mode: send
    response_header_mode: skip
    request_body_mode: streamed
    response_body_mode: buffered
    request_trailer_mode: skip
    response_trailer_mode: send
  filter_metadata:
    hello: "world"
  metadata_options:
    forwarding_namespaces:
      typed:
      - ns1
      untyped:
      - ns2
    receiving_namespaces:
      untyped:
      - ns2
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(HttpExtProcConfigTest, CorrectGrpcServiceConfigServerContext) {
  std::string yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_proc_server
      stat_prefix: google
  failure_mode_allow: true
  request_attributes:
  - 'Foo'
  - 'Bar'
  - 'Baz'
  response_attributes:
  - 'More'
  processing_mode:
    request_header_mode: send
    response_header_mode: skip
    request_body_mode: streamed
    response_body_mode: buffered
    request_trailer_mode: skip
    response_trailer_mode: send
  filter_metadata:
    hello: "world"
  metadata_options:
    forwarding_namespaces:
      typed:
      - ns1
      untyped:
      - ns2
    receiving_namespaces:
      untyped:
      - ns2
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(*proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(HttpExtProcConfigTest, CorrectHttpServiceConfigServerContext) {
  std::string yaml = R"EOF(
  http_service:
    http_service:
      http_uri:
        uri: "ext_proc_server_0:9000"
        cluster: "ext_proc_server_0"
        timeout:
          seconds: 500
  failure_mode_allow: true
  processing_mode:
    request_header_mode: send
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_CALL(context, messageValidationVisitor());
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProtoWithServerContext(*proto_config, "stats", context);
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(HttpExtProcConfigTest, CorrectRouteMetadataOnlyConfig) {
  std::string yaml = R"EOF(
  overrides:
    grpc_initial_metadata:
      - key: "a"
        value: "a"
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Router::RouteSpecificFilterConfigConstSharedPtr cb = factory.createRouteSpecificFilterConfig(
      *proto_config, context, context.messageValidationVisitor());
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
