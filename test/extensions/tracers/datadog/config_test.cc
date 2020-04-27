#include "envoy/config/trace/v3/datadog.pb.h"
#include "envoy/config/trace/v3/datadog.pb.validate.h"
#include "envoy/config/trace/v3/http_tracer.pb.h"

#include "extensions/tracers/datadog/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {
namespace {

TEST(DatadogTracerConfigTest, DatadogHttpTracer) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_CALL(context.server_factory_context_.cluster_manager_, get(Eq("fake_cluster")))
      .WillRepeatedly(
          Return(&context.server_factory_context_.cluster_manager_.thread_local_cluster_));
  ON_CALL(*context.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
          features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  const std::string yaml_string = R"EOF(
  http:
    name: datadog
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v2.DatadogConfig
      collector_cluster: fake_cluster
      service_name: fake_file
   )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  DatadogTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  Tracing::HttpTracerSharedPtr datadog_tracer = factory.createHttpTracer(*message, context);
  EXPECT_NE(nullptr, datadog_tracer);
}

} // namespace
} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
