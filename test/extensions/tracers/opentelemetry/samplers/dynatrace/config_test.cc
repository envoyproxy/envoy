#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/samplers/dynatrace/config.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

// Test creating a Dynatrace sampler via factory
TEST(DynatraceSamplerFactoryTest, Test) {
  auto* factory = Registry::FactoryRegistry<SamplerFactory>::getFactory(
      "envoy.tracers.opentelemetry.samplers.dynatrace");
  ASSERT_NE(factory, nullptr);
  EXPECT_STREQ(factory->name().c_str(), "envoy.tracers.opentelemetry.samplers.dynatrace");
  EXPECT_NE(factory->createEmptyConfigProto(), nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string sampler_yaml = R"EOF(
    name: envoy.tracers.opentelemetry.samplers.dynatrace
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.samplers.v3.DynatraceSamplerConfig
  )EOF";
  TestUtility::loadFromYaml(sampler_yaml, typed_config);

  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_NE(factory->createSampler(typed_config.typed_config(), context), nullptr);
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
