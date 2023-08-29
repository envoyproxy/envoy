#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/resource_detectors/environment/config.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

TEST(EnvironmentResourceDetectorFactoryTest, Basic) {
  auto* factory = Registry::FactoryRegistry<ResourceDetectorFactory>::getFactory(
      "envoy.tracers.opentelemetry.resource_detectors.environment");
  ASSERT_NE(factory, nullptr);

  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_NE(factory->createResourceDetector(context), nullptr);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
