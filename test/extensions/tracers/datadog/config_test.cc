#include "extensions/tracers/datadog/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Datadog {

TEST(DatadogTracerConfigTest, DatadogHttpTracer) {
  NiceMock<Server::MockInstance> server;
  EXPECT_CALL(server.cluster_manager_, get("fake_cluster"))
      .WillRepeatedly(Return(&server.cluster_manager_.thread_local_cluster_));
  ON_CALL(*server.cluster_manager_.thread_local_cluster_.cluster_.info_, features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  const std::string yaml_string = R"EOF(
  http:
    name: envoy.tracers.datadog
    config:
      collector_cluster: fake_cluster
      service_name: fake_file
   )EOF";
  envoy::config::trace::v2::Tracing configuration;
  MessageUtil::loadFromYaml(yaml_string, configuration);

  DatadogTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(configuration.http(), factory);
  Tracing::HttpTracerPtr datadog_tracer = factory.createHttpTracer(*message, server);
  EXPECT_NE(nullptr, datadog_tracer);
}

} // namespace Datadog
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
