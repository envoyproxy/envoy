#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.h"

#include "source/extensions/stat_sinks/open_telemetry/config.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace OpenTelemetry {
namespace {

TEST(OpenTelemetryConfigTest, OpenTelemetrySinkType) {
  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(
          OpenTelemetryName);
  ASSERT_NE(factory, nullptr);

  {
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
    TestUtility::jsonConvert(sink_config, *message);

    EXPECT_THROW(factory->createStatsSink(*message, server).value(), ProtoValidationException);
  }

  {
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    sink_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("otlp_grpc");
    ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
    TestUtility::jsonConvert(sink_config, *message);

    Stats::SinkPtr sink = factory->createStatsSink(*message, server).value();
    EXPECT_NE(sink, nullptr);
    EXPECT_NE(dynamic_cast<OpenTelemetry::OpenTelemetrySink*>(sink.get()), nullptr);
  }

  {
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    sink_config.mutable_http_service()->mutable_http_uri()->set_uri(
        "https://some-o11y.com/v1/metrics");
    sink_config.mutable_http_service()->mutable_http_uri()->set_cluster("otlp_http");
    sink_config.mutable_http_service()->mutable_http_uri()->mutable_timeout()->set_seconds(10);
    ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
    TestUtility::jsonConvert(sink_config, *message);

    Stats::SinkPtr sink = factory->createStatsSink(*message, server).value();
    EXPECT_NE(sink, nullptr);
    EXPECT_NE(dynamic_cast<OpenTelemetry::OpenTelemetrySink*>(sink.get()), nullptr);
  }
}

TEST(OpenTelemetryConfigTest, OtlpOptionsTest) {
  {
    NiceMock<Server::Configuration::MockServerFactoryContext> server;
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    OtlpOptions options(sink_config, Tracers::OpenTelemetry::Resource(), server);

    // Default options
    EXPECT_FALSE(options.reportCountersAsDeltas());
    EXPECT_FALSE(options.reportHistogramsAsDeltas());
    EXPECT_TRUE(options.emitTagsAsAttributes());
    EXPECT_TRUE(options.useTagExtractedName());
    EXPECT_EQ("", options.statPrefix());
    EXPECT_TRUE(options.resource_attributes().empty());
  }

  {
    NiceMock<Server::Configuration::MockServerFactoryContext> server;
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    sink_config.mutable_emit_tags_as_attributes()->set_value(false);
    sink_config.mutable_use_tag_extracted_name()->set_value(false);
    sink_config.set_prefix("prefix");

    Tracers::OpenTelemetry::Resource resource;
    resource.attributes_["key"] = "value";
    OtlpOptions options(sink_config, resource, server);
    EXPECT_FALSE(options.reportCountersAsDeltas());
    EXPECT_FALSE(options.reportHistogramsAsDeltas());
    EXPECT_FALSE(options.emitTagsAsAttributes());
    EXPECT_FALSE(options.useTagExtractedName());
    EXPECT_EQ("prefix.", options.statPrefix());
    ASSERT_EQ(1, options.resource_attributes().size());
    EXPECT_EQ("key", options.resource_attributes()[0].key());
    EXPECT_EQ("value", options.resource_attributes()[0].value().string_value());
  }
}

// Verify that the factory injects service.instance.id and service.namespace from
// LocalInfo into the resource attributes when creating the sink, so consumers can
// identify the originating Envoy instance.
TEST(OpenTelemetryConfigTest, NodeAttributesAutoPopulatedFromLocalInfo) {
  NiceMock<Server::Configuration::MockServerFactoryContext> server;

  // Set a real node id and cluster on the local_info mock.
  server.local_info_.node_.set_id("test-node-42");
  server.local_info_.node_.set_cluster("my-cluster");

  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(
          OpenTelemetryName);
  ASSERT_NE(factory, nullptr);

  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
  sink_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("otlp_grpc");
  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  // Factory must succeed and produce a valid sink.
  auto sink_or = factory->createStatsSink(*message, server);
  ASSERT_TRUE(sink_or.ok());
  EXPECT_NE(sink_or.value(), nullptr);
}

// Verify that when a resource detector already sets service.instance.id, it takes
// priority over the auto-populated value from LocalInfo (try_emplace semantics).
TEST(OpenTelemetryConfigTest, NodeAttributesNotOverriddenByAutoPopulation) {
  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;

  // Manually build a resource as if a detector already set service.instance.id.
  Tracers::OpenTelemetry::Resource resource;
  resource.attributes_["service.instance.id"] = "detector-set-id";
  resource.attributes_["service.namespace"] = "detector-set-cluster";

  OtlpOptions options(sink_config, resource, server);
  std::string instance_id_val;
  std::string namespace_val;
  for (const auto& attr : options.resource_attributes()) {
    if (attr.key() == "service.instance.id") {
      instance_id_val = attr.value().string_value();
    } else if (attr.key() == "service.namespace") {
      namespace_val = attr.value().string_value();
    }
  }
  // Detector-set values must be preserved.
  EXPECT_EQ("detector-set-id", instance_id_val);
  EXPECT_EQ("detector-set-cluster", namespace_val);
}

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
