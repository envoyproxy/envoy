#include "envoy/config/trace/v3/http_tracer.pb.h"
#include "envoy/config/trace/v3/lightstep.pb.h"
#include "envoy/config/trace/v3/lightstep.pb.validate.h"

#include "source/extensions/tracers/lightstep/config.h"

#include "test/mocks/server/tracer_factory.h"
#include "test/mocks/server/tracer_factory_context.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {
namespace {

TEST(LightstepTracerConfigTest, DEPRECATED_FEATURE_TEST(LightstepHttpTracer)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  ON_CALL(*context.server_factory_context_.cluster_manager_.active_clusters_["fake_cluster"]->info_,
          features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  const std::string yaml_string = R"EOF(
  http:
    name: lightstep
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.LightstepConfig
      collector_cluster: fake_cluster
      access_token_file: fake_file
   )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  LightstepTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto lightstep_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, lightstep_tracer);
}

TEST(LightstepTracerConfigTest, LightstepHttpTracerAccessToken) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  context.server_factory_context_.cluster_manager_.initializeClusters({"fake_cluster"}, {});
  ON_CALL(*context.server_factory_context_.cluster_manager_.active_clusters_["fake_cluster"]->info_,
          features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  const std::string yaml_string = R"EOF(
  http:
    name: lightstep
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.LightstepConfig
      collector_cluster: fake_cluster
      access_token:
        inline_string: fake_token
   )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  LightstepTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto lightstep_tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, lightstep_tracer);
}

} // namespace
} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
