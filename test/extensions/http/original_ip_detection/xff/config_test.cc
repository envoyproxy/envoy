#include "envoy/extensions/http/original_ip_detection/xff/v3/xff.pb.h"
#include "envoy/registry/registry.h"

#include "source/extensions/http/original_ip_detection/xff/config.h"

#include "test/mocks/server/factory_context.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace Xff {

TEST(CustomHeaderFactoryTest, Basic) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::OriginalIPDetectionFactory>::getFactory(
      "envoy.http.original_ip_detection.xff");
  ASSERT_NE(factory, nullptr);

  envoy::config::core::v3::TypedExtensionConfig typed_config;
  const std::string yaml = R"EOF(
    name: envoy.formatter.TestFormatter
    typed_config:
        "@type": type.googleapis.com/envoy.extensions.http.original_ip_detection.xff.v3.XffConfig
        xff_num_trusted_hops: 1
)EOF";
  TestUtility::loadFromYaml(yaml, typed_config);

  NiceMock<Server::Configuration::MockFactoryContext> context;
  EXPECT_NE(*factory->createExtension(typed_config.typed_config(), context), nullptr);
}

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
