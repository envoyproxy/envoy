#include "envoy/extensions/http/unified_header_validators/envoy_default/v3/unified_header_validator.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/http/unified_header_validators/envoy_default/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace UnifiedHeaderValidators {
namespace EnvoyDefault {

TEST(EnvoyDefaultUhvFactoryTest, Basic) {
  auto* factory =
      Registry::FactoryRegistry<Envoy::Http::UnifiedHeaderValidatorFactoryConfig>::getFactory(
          "envoy.http.unified_header_validators.envoy_default");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.http.unified_header_validators.envoy_default
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.unified_header_validators.envoy_default.v3.UnifiedHeaderValidatorConfig
        http1_protocol_options:
          http1_allow_chunked_length: true
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_NE(factory->createFromProto(typed_config.typed_config(), context), nullptr);
}

} // namespace EnvoyDefault
} // namespace UnifiedHeaderValidators
} // namespace Http
} // namespace Extensions
} // namespace Envoy
