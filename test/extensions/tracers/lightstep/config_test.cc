#include "envoy/config/trace/v3/trace.pb.h"
#include "envoy/config/trace/v3/trace.pb.validate.h"

#include "extensions/tracers/lightstep/config.h"

#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Lightstep {
namespace {

TEST(LightstepTracerConfigTest, LightstepHttpTracer) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_CALL(context.server_factory_context_.cluster_manager_, get(Eq("fake_cluster")))
      .WillRepeatedly(
          Return(&context.server_factory_context_.cluster_manager_.thread_local_cluster_));
  ON_CALL(*context.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
          features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  const std::string yaml_string = R"EOF(
  http:
    name: lightstep
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v2.LightstepConfig
      collector_cluster: fake_cluster
      access_token_file: fake_file
   )EOF";
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  LightstepTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  Tracing::HttpTracerSharedPtr lightstep_tracer = factory.createHttpTracer(*message, context);
  EXPECT_NE(nullptr, lightstep_tracer);
}

// Test that the deprecated extension name still functions.
TEST(LightstepTracerConfigTest, DEPRECATED_FEATURE_TEST(DeprecatedExtensionFilterName)) {
  const std::string deprecated_name = "envoy.lightstep";

  ASSERT_NE(nullptr, Registry::FactoryRegistry<Server::Configuration::TracerFactory>::getFactory(
                         deprecated_name));
}

} // namespace
} // namespace Lightstep
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
