#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.h"

#include "source/extensions/stat_sinks/open_telemetry/config.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

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
    EXPECT_NE(dynamic_cast<OpenTelemetry::OpenTelemetryGrpcSink*>(sink.get()), nullptr);
  }
}

TEST(OpenTelemetryConfigTest, OtlpOptionsTest) {
  {
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    OtlpOptions options(sink_config, Tracers::OpenTelemetry::Resource());

    // Default options
    EXPECT_FALSE(options.reportCountersAsDeltas());
    EXPECT_FALSE(options.reportHistogramsAsDeltas());
    EXPECT_TRUE(options.emitTagsAsAttributes());
    EXPECT_TRUE(options.useTagExtractedName());
    EXPECT_EQ("", options.statPrefix());
    EXPECT_TRUE(options.resource_attributes().empty());
  }

  {
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    sink_config.mutable_emit_tags_as_attributes()->set_value(false);
    sink_config.mutable_use_tag_extracted_name()->set_value(false);
    sink_config.set_prefix("prefix");

    Tracers::OpenTelemetry::Resource resource;
    resource.attributes_["key"] = "value";
    OtlpOptions options(sink_config, resource);
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

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
