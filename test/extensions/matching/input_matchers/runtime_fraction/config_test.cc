#include "source/extensions/matching/input_matchers/runtime_fraction/config.h"

#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace RuntimeFraction {

TEST(ConfigTest, TestConfig) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  const std::string yaml_string = R"EOF(
    name: hashing
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.input_matchers.runtime_fraction.v3.RuntimeFraction
        runtime_fraction:
          default_value:
            numerator: 50
            denominator: MILLION
          runtime_key: "some_key"
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  Config factory;
  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto matcher = factory.createInputMatcherFactoryCb(*message, factory_context);
  ASSERT_NE(nullptr, matcher);
  matcher();
}

TEST(ConfigTest, InvalidConfig) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;

  const std::string yaml_string = R"EOF(
    name: hashing
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.input_matchers.runtime_fraction.v3.RuntimeFraction
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  Config factory;
  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), factory);
  EXPECT_THROW_WITH_REGEX(factory.createInputMatcherFactoryCb(*message, factory_context),
                          EnvoyException, "RuntimeFraction: value is required");
}
} // namespace RuntimeFraction
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
