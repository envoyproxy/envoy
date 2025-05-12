#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/http/header_validators/envoy_default/config.h"
#include "source/extensions/http/header_validators/envoy_default/header_validator_factory.h"

#include "test/mocks/server/instance.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

using ::envoy::extensions::http::header_validators::envoy_default::v3::HeaderValidatorConfig;

TEST(EnvoyDefaultUhvFactoryTest, Basic) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::HeaderValidatorFactoryConfig>::getFactory(
      "envoy.http.header_validators.envoy_default");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.http.header_validators.envoy_default
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.header_validators.envoy_default.v3.HeaderValidatorConfig
        http1_protocol_options:
          allow_chunked_length: true
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);

  ::testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  EXPECT_NE(factory->createFromProto(typed_config.typed_config(), server_context), nullptr);
}

TEST(EnvoyDefaultUhvFactoryTest, PathWithEscapedSlashesDefaultValue) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::HeaderValidatorFactoryConfig>::getFactory(
      "envoy.http.header_validators.envoy_default");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.http.header_validators.envoy_default
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.header_validators.envoy_default.v3.HeaderValidatorConfig
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);

  ::testing::NiceMock<Server::Configuration::MockServerFactoryContext> server_context;
  auto uhv_factory = factory->createFromProto(typed_config.typed_config(), server_context);
  // Cast to the expected type to examine configuration values
  auto* uhv_factory_ptr = static_cast<
      ::Envoy::Extensions::Http::HeaderValidators::EnvoyDefault::HeaderValidatorFactory*>(
      uhv_factory.get());
  // Expect that the IMPLEMENTATION_SPECIFIC_DEFAULT (the default enum value) was replaced with the
  // KEEP_UNCHANGED
  EXPECT_EQ(uhv_factory_ptr->getConfigurationForTestsOnly()
                .uri_path_normalization_options()
                .path_with_escaped_slashes_action(),
            HeaderValidatorConfig::UriPathNormalizationOptions::KEEP_UNCHANGED);
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
