#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/samplers/parent_based/config.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

// Test create sampler via factory
TEST(ParentBasedSamplerFactoryTest, Test) {
  auto* factory = Registry::FactoryRegistry<SamplerFactory>::getFactory(
      "envoy.tracers.opentelemetry.samplers.parent_based");
  ASSERT_NE(factory, nullptr);
  EXPECT_STREQ(factory->name().c_str(), "envoy.tracers.opentelemetry.samplers.parent_based");
  EXPECT_NE(factory->createEmptyConfigProto(), nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.tracers.opentelemetry.samplers.parent_based
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.samplers.v3.ParentBasedSamplerConfig
        wrapped_sampler:
            name: envoy.tracers.opentelemetry.samplers.trace_id_ratio_based
            typed_config:
                "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.samplers.v3.TraceIdRatioBasedSamplerConfig
                sampling_percentage:
                    numerator: 20
                    denominator: TEN_THOUSAND
  )EOF";
  TestUtility::loadFromYaml(yaml, typed_config);
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_NE(factory->createSampler(typed_config.typed_config(), context), nullptr);
  EXPECT_STREQ(factory->name().c_str(), "envoy.tracers.opentelemetry.samplers.parent_based");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
