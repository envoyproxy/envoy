#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/tracers/fluentd/config.h"

#include "test/mocks/server/tracer_factory.h"
#include "test/mocks/server/tracer_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Fluentd {

// Configure with only required fields
TEST(FluentdTracerConfigTest, FluentdTracerMinimalConfig) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;

  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  ON_CALL(context.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillByDefault(testing::Return(&thread_local_cluster));

  auto client = std::make_unique<NiceMock<Envoy::Tcp::AsyncClient::MockAsyncTcpClient>>();
  ON_CALL(thread_local_cluster, tcpAsyncClient(_, _))
      .WillByDefault(testing::Return(testing::ByMove(std::move(client))));

  Envoy::Extensions::Tracers::Fluentd::FluentdTracerFactory factory;

  const std::string yaml_json = R"EOF(
      http:
        name: envoy.tracers.fluentd
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.tracers.fluentd.v3.FluentdConfig
          cluster: "fake_cluster"
          tag: "fake_tag"
          stat_prefix: "envoy.tracers.fluentd"
    )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_json, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto fluentd_tracer = factory.createTracerDriver(*message, context);

  EXPECT_NE(nullptr, fluentd_tracer);
}

// Configure with all fields
TEST(FluentdTracerConfigTest, FluentdTracerFullConfig) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;

  NiceMock<Upstream::MockThreadLocalCluster> thread_local_cluster;
  ON_CALL(context.server_factory_context_.cluster_manager_, getThreadLocalCluster(_))
      .WillByDefault(testing::Return(&thread_local_cluster));

  auto client = std::make_unique<NiceMock<Envoy::Tcp::AsyncClient::MockAsyncTcpClient>>();
  ON_CALL(thread_local_cluster, tcpAsyncClient(_, _))
      .WillByDefault(testing::Return(testing::ByMove(std::move(client))));
  Envoy::Extensions::Tracers::Fluentd::FluentdTracerFactory factory;

  const std::string yaml_json = R"EOF(
      http:
        name: envoy.tracers.fluentd
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.tracers.fluentd.v3.FluentdConfig
          cluster: "fake_cluster"
          tag: "fake_tag"
          stat_prefix: "envoy.tracers.fluentd"
          buffer_flush_interval: 0.0001s
          buffer_size_bytes: 16384
          retry_policy:
            num_retries: 1024
            retry_back_off:
              base_interval: 0.5s
              max_interval: 5s
    )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_json, configuration);

  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto fluentd_tracer = factory.createTracerDriver(*message, context);

  EXPECT_NE(nullptr, fluentd_tracer);
}

} // namespace Fluentd
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
