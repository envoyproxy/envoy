#include "source/common/config/utility.h"
#include "source/extensions/matching/common_inputs/environment_variable/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Matching {
namespace CommonInputs {
namespace EnvironmentVariable {

TEST(ConfigTest, TestConfig) {
  const std::string yaml_string = R"EOF(
    name: hashing
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.matching.common_inputs.environment_variable.v3.Config
        name: foo
)EOF";

  envoy::config::core::v3::TypedExtensionConfig config;
  TestUtility::loadFromYaml(yaml_string, config);

  Config factory;
  auto message = Envoy::Config::Utility::translateAnyToFactoryConfig(
      config.typed_config(), ProtobufMessage::getStrictValidationVisitor(), factory);

  {
    auto input_factory = factory.createCommonProtocolInputFactoryCb(
        *message, ProtobufMessage::getStrictValidationVisitor());
    EXPECT_NE(nullptr, input_factory);
    EXPECT_TRUE(absl::holds_alternative<absl::monostate>(input_factory()->get()));
  }

  TestEnvironment::setEnvVar("foo", "bar", 1);
  {
    auto input_factory = factory.createCommonProtocolInputFactoryCb(
        *message, ProtobufMessage::getStrictValidationVisitor());
    EXPECT_NE(nullptr, input_factory);
    EXPECT_EQ(absl::get<std::string>(input_factory()->get()), "bar");
  }

  TestEnvironment::unsetEnvVar("foo");
}

} // namespace EnvironmentVariable
} // namespace CommonInputs
} // namespace Matching
} // namespace Extensions
} // namespace Envoy
