#include "envoy/registry/registry.h"

#include "source/extensions/tracers/opentelemetry/samplers/cel/config.h"

#include "test/mocks/server/tracer_factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace OpenTelemetry {

// Test create sampler via factory
TEST(CELSamplerFactoryTest, TestParsedExpr) {
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
  expression:
    cel_expr_parsed:
      expr:
        id: 3
        call_expr:
          function: _==_
          args:
          - id: 2
            select_expr:
              operand:
                id: 1
                ident_expr:
                  name: request
              field: path
          - id: 4
            const_expr:
              string_value: "/test-1234-deny"
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_NE(factory->createSampler(typed_config.typed_config(), context), nullptr);
  EXPECT_STREQ(factory->name().c_str(), "envoy.tracers.opentelemetry.samplers.cel");
}

TEST(CELSamplerFactoryTest, TestCheckedExpr) {
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
  expression:
    cel_expr_checked:
      expr:
        id: 3
        call_expr:
          function: _==_
          args:
          - id: 2
            select_expr:
              operand:
                id: 1
                ident_expr:
                  name: request
              field: path
          - id: 4
            const_expr:
              string_value: "/test-1234-deny"
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_NE(factory->createSampler(typed_config.typed_config(), context), nullptr);
  EXPECT_STREQ(factory->name().c_str(), "envoy.tracers.opentelemetry.samplers.cel");
}

TEST(CELSamplerFactoryTest, TestEmptyExpr) {
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
        expression: {}
  )EOF";
  TestUtility::loadFromYaml(yaml, typed_config);
  NiceMock<Server::Configuration::MockTracerFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(factory->createSampler(typed_config.typed_config(), context),
                          EnvoyException, "CEL expression not set.*");
}

} // namespace OpenTelemetry
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
