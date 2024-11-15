#include "source/extensions/matching/input_matchers/consistent_hashing/config.h"

#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace ConsistentHashing {

TEST(ConfigTest, TestConfig) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  const std::string yaml_string = R"EOF(
    name: hashing
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.input_matchers.consistent_hashing.v3.ConsistentHashing
        modulo: 100
        threshold: 10
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  ConsistentHashingConfig factory;
  auto message = Config::Utility::translateAnyToFactoryConfig(
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
        "@type": type.googleapis.com/envoy.extensions.matching.input_matchers.consistent_hashing.v3.ConsistentHashing
        modulo: 100
        threshold: 200
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  ConsistentHashingConfig factory;
  auto message = Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), factory);
  EXPECT_THROW_WITH_MESSAGE(factory.createInputMatcherFactoryCb(*message, factory_context),
                            EnvoyException, "threshold cannot be greater than modulo: 200 > 100");
}
} // namespace ConsistentHashing
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
