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
      Registry::FactoryRegistry<Server::Configuration::StatsSinkFactory>::getFactory(OpenTelemetryName);
  ASSERT_NE(factory, nullptr);

  {
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    sink_config.set_http_service("otlp_http_cluster"); // not supported
    ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
    TestUtility::jsonConvert(sink_config, *message);

    EXPECT_THROW(factory->createStatsSink(*message, server), EnvoyException);
  }

  {
    envoy::extensions::stat_sinks::open_telemetry::v3::SinkConfig sink_config;
    sink_config.mutable_grpc_service()->mutable_envoy_grpc()->set_cluster_name("otlp_grpc");
    ProtobufTypes::MessagePtr message = factory->createEmptyConfigProto();
    TestUtility::jsonConvert(sink_config, *message);

    Stats::SinkPtr sink = factory->createStatsSink(*message, server);
    EXPECT_NE(sink, nullptr);
    EXPECT_NE(dynamic_cast<OpenTelemetry::OpenTelemetryGrpcSink*>(sink.get()), nullptr);
  }
}

} // namespace
} // namespace OpenTelemetry
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
