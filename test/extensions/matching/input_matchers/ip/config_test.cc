#include "extensions/matching/input_matchers/ip/config.h"

#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace IP {

TEST(ConfigTest, TestConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  const std::string yaml_string = R"EOF(
    name: ip
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.input_matchers.ip.v3.IP
        cidr_ranges:
        - address_prefix: 192.0.2.0
          prefix_len: 24
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  IPConfig factory;
  auto message = Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto matcher = factory.createInputMatcher(*message, context);
  EXPECT_NE(nullptr, matcher);
}

TEST(ConfigTest, InvalidConfig) {
  NiceMock<Server::Configuration::MockFactoryContext> context;

  const std::string yaml_string = R"EOF(
    name: ip
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.input_matchers.ip.v3.IP
        cidr_ranges:
        - address_prefix: foo
          prefix_len: 10
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  IPConfig factory;
  auto message = Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), factory);
  EXPECT_THROW_WITH_MESSAGE(factory.createInputMatcher(*message, context), EnvoyException,
                            "malformed IP address: foo");
}
} // namespace IP
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
