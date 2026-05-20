#include "envoy/extensions/http/original_ip_detection/extracted_external_address/v3/extracted_external_address.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/http/original_ip_detection/extracted_external_address/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace ExtractedExternalAddress {

TEST(ExtractedExternalAddressFactoryTest, Basic) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::OriginalIPDetectionFactory>::getFactory(
      "envoy.http.original_ip_detection.extracted_external_address");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.http.original_ip_detection.extracted_external_address
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.original_ip_detection.extracted_external_address.v3.ExtractedExternalAddressConfig
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_NE(*factory->createExtension(typed_config.typed_config(), context), nullptr);
}

} // namespace ExtractedExternalAddress
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
