#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/samplers/cel/config.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

#if defined(USE_CEL_PARSER)
// Test create sampler via factory
TEST(CELSamplerFactoryTest, Test) {
  auto* factory = Registry::FactoryRegistry<SamplerFactory>::getFactory(
      "envoy.tracers.opentelemetry.samplers.cel");
  ASSERT_NE(factory, nullptr);
  EXPECT_STREQ(factory->name().c_str(), "envoy.tracers.opentelemetry.samplers.cel");
  EXPECT_NE(factory->createEmptyConfigProto(), nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.tracers.opentelemetry.samplers.cel
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.tracers.opentelemetry.samplers.v3.CELSamplerConfig
        expression: request.headers["x-foo"] == "bar"
  )EOF";
  TestUtility::loadFromYaml(yaml, typed_config);
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_NE(factory->createSampler(typed_config.typed_config(), context), nullptr);
  EXPECT_STREQ(factory->name().c_str(), "envoy.tracers.opentelemetry.samplers.cel");
}
#endif

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
