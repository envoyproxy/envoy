#include "envoy/config/trace/v3/dynamic_ot.pb.h"
#include "envoy/config/trace/v3/dynamic_ot.pb.validate.h"
#include "envoy/config/trace/v3/http_tracer.pb.h"

#include "source/extensions/tracers/dynamic_ot/config.h"

#include "test/mocks/server/tracer_factory.h"
#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/environment.h"

#include "fmt/printf.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Eq;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace DynamicOt {
namespace {

TEST(DynamicOtTracerConfigTest, DEPRECATED_FEATURE_TEST(DynamicOpentracingHttpTracer)) {
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_CALL(context.server_factory_context_.cluster_manager_,
              getThreadLocalCluster(Eq("fake_cluster")))
      .WillRepeatedly(
          Return(&context.server_factory_context_.cluster_manager_.thread_local_cluster_));
  ON_CALL(*context.server_factory_context_.cluster_manager_.thread_local_cluster_.cluster_.info_,
          features())
      .WillByDefault(Return(Upstream::ClusterInfo::Features::HTTP2));

  const std::string yaml_string = fmt::sprintf(
      R"EOF(
  http:
    name: envoy.tracers.dynamic_ot
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v3.DynamicOtConfig
      library: %s
      config:
        output_file: fake_file
  )EOF",
      TestEnvironment::runfilesPath("mocktracer/libmocktracer_plugin.so", "io_opentracing_cpp"));
  envoy::config::trace::v3::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  DynamicOpenTracingTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto tracer = factory.createTracerDriver(*message, context);
  EXPECT_NE(nullptr, tracer);
}

} // namespace
} // namespace DynamicOt
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
