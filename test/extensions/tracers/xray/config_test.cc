#include "envoy/registry/registry.h"

#include "extensions/tracers/xray/config.h"

#include "test/mocks/server/mocks.h"
#include "test/test_common/environment.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using ::testing::Throw;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace XRay {
namespace {

TEST(XRayTracerConfigTest, XRayHttpTracerWithTypedConfig) {
  NiceMock<Server::MockInstance> server;

  const std::string yaml_string = R"EOF(
  http:
    name: envoy.tracers.xray
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v2.XRayConfig
      daemon_endpoint: 127.0.0.1
      segment_name: AwsAppMesh
      sampling_rule_manifest:
        filename: "rules.json")EOF";

  envoy::config::trace::v2::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  XRayTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);
  Tracing::HttpTracerPtr xray_tracer = factory.createHttpTracer(*message, server);
  ASSERT_NE(nullptr, xray_tracer);
}

TEST(XRayTracerConfigTest, XRayHttpTracerWithInvalidFileName) {
  NiceMock<Server::MockInstance> server;
  NiceMock<Api::MockApi> api;
  NiceMock<Filesystem::MockInstance> file_system;

  // fake invalid file
  EXPECT_CALL(file_system, fileReadToEnd("rules.json"))
      .WillRepeatedly(Throw(EnvoyException("failed to open file.")));
  EXPECT_CALL(api, fileSystem()).WillRepeatedly(ReturnRef(file_system));
  EXPECT_CALL(server, api()).WillRepeatedly(ReturnRef(api));

  const std::string yaml_string = R"EOF(
  http:
    name: envoy.tracers.xray
    typed_config:
      "@type": type.googleapis.com/envoy.config.trace.v2.XRayConfig
      daemon_endpoint: 127.0.0.1
      segment_name: AwsAppMesh
      sampling_rule_manifest:
        filename: "rules.json")EOF";

  envoy::config::trace::v2::Tracing configuration;
  TestUtility::loadFromYaml(yaml_string, configuration);

  XRayTracerFactory factory;
  auto message = Config::Utility::translateToFactoryConfig(
      configuration.http(), ProtobufMessage::getStrictValidationVisitor(), factory);

  Tracing::HttpTracerPtr xray_tracer = factory.createHttpTracer(*message, server);
  ASSERT_NE(nullptr, xray_tracer);
}

} // namespace
} // namespace XRay
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
