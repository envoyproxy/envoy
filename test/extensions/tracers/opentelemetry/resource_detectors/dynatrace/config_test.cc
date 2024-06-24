#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/dynatrace/config.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

TEST(DynatraceResourceDetectorFactoryTest, Basic) {
  auto* factory = Registry::FactoryRegistry<ResourceDetectorFactory>::getFactory(
      "envoy.tracers.opentelemetry.resource_detectors.dynatrace");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.tracers.opentelemetry.resource_detectors.dynatrace
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.resource_detectors.v3.DynatraceResourceDetectorConfig
  )EOF";
  TestUtility::loadFromYaml(yaml, typed_config);

  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_NE(factory->createResourceDetector(typed_config.typed_config(), context), nullptr);
  EXPECT_STREQ(factory->name().c_str(), "envoy.tracers.opentelemetry.resource_detectors.dynatrace");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
