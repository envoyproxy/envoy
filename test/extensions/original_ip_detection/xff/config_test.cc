#include "envoy/extensions/original_ip_detection/xff/v3/xff.pb.h"
#include "envoy/registry/registry.h"

#include "extensions/original_ip_detection/xff/config.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace Xff {

TEST(CustomHeaderFactoryTest, Basic) {
  auto* factory = Registry::FactoryRegistry<Envoy::Http::OriginalIPDetectionFactory>::getFactory(
      "envoy.http.original_ip_detection.xff");
  ASSERT_NE(factory, nullptr);

  auto empty = factory->createEmptyConfigProto();
  EXPECT_NE(empty, nullptr);

  auto config =
      *dynamic_cast<envoy::extensions::original_ip_detection::xff::v3::XffConfig*>(empty.get());
  config.set_xff_num_trusted_hops(1);

  EXPECT_NE(factory->createExtension(config), nullptr);
}

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
