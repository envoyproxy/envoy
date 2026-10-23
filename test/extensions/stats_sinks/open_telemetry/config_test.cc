#include "envoy/extensions/stat_sinks/open_telemetry/v3/open_telemetry.pb.h"

#include "source/extensions/stat_sinks/open_telemetry/config.h"
#include "source/extensions/stat_sinks/open_telemetry/open_telemetry_impl.h"

#include "test/mocks/server/server_factory_context.h"
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
    Tracers::OpenTelemetry::Resource resource;
    OtlpOptionsSharedPtr options = OtlpOptions::create(sink_config, resource, server).value();

    // Default options
    EXPECT_FALSE(options->reportCountersAsDeltas());
    EXPECT_FALSE(options->reportHistogramsAsDeltas());
    EXPECT_TRUE(options->emitTagsAsAttributes());
    EXPECT_TRUE(options->useTagExtractedName());
    EXPECT_EQ("", options->statPrefix());
    EXPECT_TRUE(options->resource_attributes().empty());
  }

  {
    NiceMock<Server::Configuration::MockServerFactoryContext> server;
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    sink_config.mutable_emit_tags_as_attributes()->set_value(false);
    sink_config.mutable_use_tag_extracted_name()->set_value(false);
    sink_config.set_prefix("prefix");

    Tracers::OpenTelemetry::Resource resource;
    resource.attributes_["key"] = "value";
    OtlpOptionsSharedPtr options = OtlpOptions::create(sink_config, resource, server).value();
    EXPECT_FALSE(options->reportCountersAsDeltas());
    EXPECT_FALSE(options->reportHistogramsAsDeltas());
    EXPECT_FALSE(options->emitTagsAsAttributes());
    EXPECT_FALSE(options->useTagExtractedName());
    EXPECT_EQ("prefix.", options->statPrefix());
    ASSERT_EQ(1, options->resource_attributes().size());
    EXPECT_EQ("key", options->resource_attributes()[0].key());
    EXPECT_EQ("value", options->resource_attributes()[0].value().string_value());
  }
}

TEST(OpenTelemetryConfigTest, InvalidResourceConversion) {
  NiceMock<Server::Configuration::MockServerFactoryContext> server;
  Server::Configuration::StatsSinkFactory* factory =
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(
          OpenTelemetryName);
  ASSERT_NE(factory, nullptr);

  // Two tags of the conversion set the same resource attribute.
  envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
  TestUtility::loadFromYaml(R"EOF(
    grpc_service:
      envoy_grpc:
        cluster_name: otlp_grpc
    custom_metric_conversions:
      matcher_tree:
        input:
          name: stat_full_name_match_input
          typed_config:
            "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.stats.v3.StatFullNameMatchInput
        prefix_match_map:
          map:
            "workload.":
              action:
                name: convert
                typed_config:
                  "@type": type.googleapis.com/envoy.extensions.stat_sinks.open_telemetry.v3.SinkConfig.ConversionAction
                  resource_conversion:
                    tags_to_resource_attributes:
                    - {tag_name: pod, resource_attribute_key: name}
                    - {tag_name: service, resource_attribute_key: name}
  )EOF",
                            sink_config);
  ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
  TestUtility::jsonConvert(sink_config, *message);

  const absl::StatusOr<Stats::SinkPtr> sink = factory->createStatsSink(*message, server);
  EXPECT_EQ(absl::StatusCode::kInvalidArgument, sink.status().code());
  EXPECT_EQ("resource_conversion: resource attribute 'name' is set more than once",
            sink.status().message());
}

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
