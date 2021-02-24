#include "envoy/extensions/original_ip_detection/custom_header/v3/custom_header.pb.h"
#include "envoy/registry/registry.h"

#include "extensions/original_ip_detection/custom_header/config.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace CustomHeader {

TEST(CustomHeaderFactoryTest, Basic) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::OriginalIPDetectionFactory>::getFactory(
      "envoy.http.original_ip_detection.custom_header");
  ASSERT_NE(factory, nullptr);

  auto empty = factory->createEmptyConfigProto();
  EXPECT_NE(empty, nullptr);

  auto config = *dynamic_cast<
      envoy::extensions::original_ip_detection::custom_header::v3::CustomHeaderConfig*>(
      empty.get());
  config.set_header_name("x-real-ip");

  EXPECT_NE(factory->createExtension(config), nullptr);
}

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
