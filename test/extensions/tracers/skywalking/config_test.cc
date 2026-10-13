#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/config/trace/v3/skywalking.pb.h"
#include "envoy/config/trace/v3/skywalking.pb.validate.h"

#include "source/extensions/tracers/skywalking/config.h"

#include "test/mocks/server/tracer_factory.h"
#include "test/mocks/server/tracer_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace SkyWalking {
namespace {

TEST(SkyWalkingTracerConfigTest, SkyWalkingHttpTracer) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_CALL(context.server_factory_context_.cluster_manager_,
              getThreadLocalCluster(Eq("fake_cluster")))
      .WillRepeatedly(
          Return(&context.server_factory_context_.cluster_manager_.thread_local_cluster_));
  ON_CALL(*context.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
          features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  const std::string yaml_string = R"EOF(
  http:
    name: skywalking
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.SkyWalkingConfig
      grpc_service:
        envoy_grpc:
          cluster_name: fake_cluster
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  SkyWalkingTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto skywalking_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, skywalking_tracer);
}

TEST(SkyWalkingTracerConfigTest, SkyWalkingHttpTracerWithClientConfig) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_CALL(context.server_factory_context_.cluster_manager_,
              getThreadLocalCluster(Eq("fake_cluster")))
      .WillRepeatedly(
          Return(&context.server_factory_context_.cluster_manager_.thread_local_cluster_));
  ON_CALL(*context.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
          features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  const std::string yaml_string = R"EOF(
  http:
    name: skywalking
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.SkyWalkingConfig
      grpc_service:
        envoy_grpc:
          cluster_name: fake_cluster
      client_config:
        backend_token: "A fake auth string for SkyWalking test"
        service_name: "Test Service"
        instance_name: "Test Instance"
        max_cache_size: 2333
  )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  SkyWalkingTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto skywalking_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, skywalking_tracer);
}

} // namespace
} // namespace SkyWalking
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
