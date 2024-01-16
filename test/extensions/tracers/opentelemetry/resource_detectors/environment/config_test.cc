#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/environment/config.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

// Test create resource detector via factory
TEST(EnvironmentResourceDetectorFactoryTest, Basic) {
  auto* factory = Registry::FactoryRegistry<ResourceDetectorFactory>::getFactory(
      "envoy.tracers.opentelemetry.resource_detectors.environment");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.tracers.opentelemetry.resource_detectors.environment
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.resource_detectors.v3.EnvironmentResourceDetectorConfig
  )EOF";
  TestUtility::loadFromYaml(yaml, typed_config);

  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_NE(factory->createResourceDetector(typed_config.typed_config(), context), nullptr);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
