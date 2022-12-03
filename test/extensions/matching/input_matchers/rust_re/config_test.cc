#include "source/extensions/matching/input_matchers/rust_re/config.h"

#include "test/mocks/server/factory_context.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace InputMatchers {
namespace RustRe {

TEST(ConfigTest, TestConfig) {
  NiceMock<Server::Configuration::MockServerFactoryContext> factory_context;
  const std::string yaml_string = R"EOF(
    name: hashing
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.input_matchers.rust_re.v3.RustRe
        regex: .*
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  RustReConfig factory;
  auto message = Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), factory);
  auto matcher = factory.createInputMatcherFactoryCb(*message, factory_context);
  ASSERT_NE(nullptr, matcher);
  matcher();
}

} // namespace RustRe
} // namespace InputMatchers
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
