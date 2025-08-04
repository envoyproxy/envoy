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
  Router::RouteSpecificFilterConfigConstSharedPtr cb =
      factory
          .createRouteSpecificFilterConfig(*proto_config, context,
                                           context.messageValidationVisitor())
          .value();
}

TEST(HttpExtProcConfigTest, InvalidServiceConfig) {
  std::string yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_proc_server
  http_service:
    http_service:
      http_uri:
        uri: "ext_proc_server_0:9000"
        cluster: "ext_proc_server_0"
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).value(), EnvoyException,
      "Proto constraint validation failed \\(ExternalProcessorValidationError.GrpcService.*");
}

TEST(HttpExtProcConfigTest, InvalidHttpServiceProcessingMode) {
  std::string yaml = R"EOF(
  http_service:
    http_service:
      http_uri:
        uri: "ext_proc_server_0:9000"
        cluster: "ext_proc_server_0"
        timeout:
          seconds: 500
  processing_mode:
    request_body_mode: streamed
    response_body_mode: buffered
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "If the ext_proc filter is configured with http_service instead of gRPC service, "
            "then the processing modes of this filter can not be configured to send body or "
            "trailer.");
}

TEST(HttpExtProcConfigTest, HttpServiceTrailerProcessingModeNotSKIP) {
  std::string yaml = R"EOF(
  http_service:
    http_service:
      http_uri:
        uri: "ext_proc_server_0:9000"
        cluster: "ext_proc_server_0"
        timeout:
          seconds: 500
  processing_mode:
    request_body_mode: "NONE"
    response_body_mode: "NONE"
    request_trailer_mode: "SKIP"
    response_trailer_mode: "SEND"
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "If the ext_proc filter is configured with http_service instead of gRPC service, "
            "then the processing modes of this filter can not be configured to send body or "
            "trailer.");
}

TEST(HttpExtProcConfigTest, InvalidFullDuplexStreamedConfig) {
  std::string yaml = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: ext_proc_server
  processing_mode:
    request_body_mode: FULL_DUPLEX_STREAMED
    request_trailer_mode: skip
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "If the ext_proc filter has the request_body_mode set to FULL_DUPLEX_STREAMED, "
            "then the request_trailer_mode has to be set to SEND");
}

TEST(HttpExtProcConfigTest, GrpcServiceHttpServiceBothSet) {
  std::string yaml = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  http_service:
    http_service:
      http_uri:
        uri: "ext_proc_server_0:9000"
        cluster: "ext_proc_server_0"
        timeout:
          seconds: 500
  processing_mode:
    response_body_mode: "BUFFERED"
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(),
            "One and only one of grpc_service or http_service must be configured");
}

// Verify that the "disable_route_cache_clearing" and "route_cache_action"  setting
// can not be set at the same time.
TEST(HttpExtProcConfigTest, InvalidRouteCacheConfig) {
  std::string yaml = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: ext_proc_server
  disable_clear_route_cache: true
  route_cache_action: RETAIN
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(result.status().message(), "disable_clear_route_cache and route_cache_action can not "
                                       "be set to none-default at the same time.");
}

TEST(HttpExtProcConfigTest, InvalidServiceConfigServerContext) {
  std::string yaml = R"EOF(
  grpc_service:
    envoy_grpc:
      cluster_name: "ext_proc_server"
  http_service:
    http_service:
      http_uri:
        uri: "ext_proc_server_0:9000"
        cluster: "ext_proc_server_0"
        timeout:
          seconds: 500
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  EXPECT_THROW_WITH_MESSAGE(
      factory.createFilterFactoryFromProtoWithServerContext(*proto_config, "stats", context),
      EnvoyException, "One and only one of grpc_service or http_service must be configured");
}

TEST(HttpExtProcConfigTest, EmptyConfig) {
  std::string yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_proc_server
      stat_prefix: google
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

TEST(HttpExtProcConfigTest, CorrectHttpServiceConfig) {
  std::string yaml = R"EOF(
  http_service:
    http_service:
      http_uri:
        uri: "ext_proc_server_0:9000"
        cluster: "ext_proc_server_0"
        timeout:
          seconds: 500
  processing_mode:
    request_header_mode: send
    response_header_mode: skip
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

TEST(HttpExtProcConfigTest, UpstreamConfig) {
  std::string yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_proc_server
      stat_prefix: google
  processing_mode:
    request_header_mode: send
    response_header_mode: skip
  )EOF";

  UpstreamExternalProcessingFilterConfig factory;
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

TEST(HttpExtProcConfigTest, CustomTimeoutConfig) {
  std::string yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_proc_server
      stat_prefix: google
  message_timeout:
    seconds: 2
    nanos: 500000000
  max_message_timeout:
    seconds: 5
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  Http::FilterFactoryCb cb =
      factory.createFilterFactoryFromProto(*proto_config, "stats", context).value();
  Http::MockFilterChainFactoryCallbacks filter_callback;
  EXPECT_CALL(filter_callback, addStreamFilter(_));
  cb(filter_callback);
}

TEST(HttpExtProcConfigTest, CompleteRouteConfig) {
  std::string yaml = R"EOF(
  overrides:
    request_attributes:
    - "route-attr1"
    - "route-attr2"
    response_attributes:
    - "route-resp1"
    processing_mode:
      request_header_mode: skip
      response_header_mode: send
      request_body_mode: none
      response_body_mode: none
    grpc_initial_metadata:
      - key: "route-meta1"
        value: "value1"
      - key: "route-meta2"
        value: "value2"
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyRouteConfigProto();
  TestUtility::loadFromYaml(yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockServerFactoryContext> context;
  Router::RouteSpecificFilterConfigConstSharedPtr cb =
      factory
          .createRouteSpecificFilterConfig(*proto_config, context,
                                           context.messageValidationVisitor())
          .value();
  ASSERT_NE(nullptr, cb);
}

TEST(HttpExtProcConfigTest, FullDuplexStreamedValidation) {
  // Valid configuration: FULL_DUPLEX_STREAMED with SEND trailers
  std::string valid_yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_proc_server
      stat_prefix: google
  processing_mode:
    response_body_mode: FULL_DUPLEX_STREAMED
    response_trailer_mode: SEND
  )EOF";

  ExternalProcessingFilterConfig factory;
  ProtobufTypes::MessagePtr proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(valid_yaml, *proto_config);

  testing::NiceMock<Server::Configuration::MockFactoryContext> context;
  auto result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_TRUE(result.ok());

  // Invalid configuration: FULL_DUPLEX_STREAMED with SKIP trailers
  std::string invalid_yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_proc_server
      stat_prefix: google
  processing_mode:
    response_body_mode: FULL_DUPLEX_STREAMED
    response_trailer_mode: SKIP
  )EOF";

  proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(invalid_yaml, *proto_config);

  auto invalid_result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_FALSE(invalid_result.ok());
  EXPECT_EQ(invalid_result.status().message(),
            "If the ext_proc filter has the response_body_mode set to FULL_DUPLEX_STREAMED, "
            "then the response_trailer_mode has to be set to SEND");

  // Verify other response_body_mode values work with SKIP trailers
  std::string other_modes_yaml = R"EOF(
  grpc_service:
    google_grpc:
      target_uri: ext_proc_server
      stat_prefix: google
  processing_mode:
    response_body_mode: BUFFERED
    response_trailer_mode: SKIP
  )EOF";

  proto_config = factory.createEmptyConfigProto();
  TestUtility::loadFromYaml(other_modes_yaml, *proto_config);

  auto other_result = factory.createFilterFactoryFromProto(*proto_config, "stats", context);
  EXPECT_TRUE(other_result.ok());
}

} // namespace
} // namespace ExternalProcessing
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
