#include "envoy/extensions/http/header_validators/envoy_default/v3/header_validator.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/http/header_validators/envoy_default/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace HeaderValidators {
namespace EnvoyDefault {

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

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_NE(factory->createFromProto(typed_config.typed_config(), context), nullptr);
}

} // namespace EnvoyDefault
} // namespace HeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
