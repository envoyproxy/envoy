#include "envoy/extensions/http/original_ip_detection/custom_header/v3/custom_header.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/http/original_ip_detection/custom_header/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace CustomHeader {

TEST(CustomHeaderFactoryTest, Basic) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::OriginalIPDetectionFactory>::getFactory(
      "envoy.http.original_ip_detection.custom_header");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.formatter.TestFormatter
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig
        header_name: x-real-ip
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_NE(*factory->createExtension(typed_config.typed_config(), context), nullptr);
}

TEST(CustomHeaderFactoryTest, InvalidHeaderName) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::OriginalIPDetectionFactory>::getFactory(
      "envoy.http.original_ip_detection.custom_header");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.formatter.TestFormatter
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.original_ip_detection.custom_header.v3.CustomHeaderConfig
        header_name: "   "
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_THROW_WITH_REGEX(*factory->createExtension(typed_config.typed_config(), context),
                          EnvoyException,
                          "Proto constraint validation failed.*does not match regex pattern.*");
}

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
