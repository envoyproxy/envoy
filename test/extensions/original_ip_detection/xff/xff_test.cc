#include "envoy/extensions/original_ip_detection/xff/v3/xff.pb.h"

#include "extensions/original_ip_detection/xff/xff.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace Xff {

class XffTest : public testing::Test {
protected:
  XffTest() {
    envoy::extensions::original_ip_detection::xff::v3::XffConfig config;
    config.set_xff_num_trusted_hops(1);
    xff_extension_ = std::make_shared<XffIPDetection>(config);
  }

  std::shared_ptr<XffIPDetection> xff_extension_;
};

TEST_F(XffTest, Detection) {
  // Header missing.
  {
    Http::TestRequestHeaderMapImpl headers{{"x-other", "abc"}};
    Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = xff_extension_->detect(params);

    EXPECT_EQ(nullptr, result.detected_remote_address);
    EXPECT_FALSE(result.allow_trusted_address_checks);
  }

  // Good request.
  {
    Http::TestRequestHeaderMapImpl headers{{"x-forwarded-for", "1.2.3.4,2.2.2.2"}};
    Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = xff_extension_->detect(params);

    EXPECT_EQ("1.2.3.4:0", result.detected_remote_address->asString());
    EXPECT_FALSE(result.allow_trusted_address_checks);
  }
}

} // namespace Xff
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
