#include "envoy/extensions/original_ip_detection/custom_header/v3/custom_header.pb.h"

#include "extensions/original_ip_detection/custom_header/custom_header.h"

#include "common/network/utility.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace OriginalIPDetection {
namespace CustomHeader {

class CustomHeaderTest : public testing::Test {
protected:
  CustomHeaderTest() {
    envoy::extensions::original_ip_detection::custom_header::v3::CustomHeaderConfig config;
    config.set_header_name("x-real-ip");
    config.set_allow_trusted_address_checks(true);
    custom_header_extension_ = std::make_shared<CustomHeaderIPDetection>(config);
  }

  std::shared_ptr<CustomHeaderIPDetection> custom_header_extension_;
};

TEST_F(CustomHeaderTest, Detection) {
  // Header missing.
  {
    Http::TestRequestHeaderMapImpl headers{{"x-other", "abc"}};
    Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = custom_header_extension_->detect(params);

    EXPECT_EQ(nullptr, result.detected_remote_address);
    EXPECT_FALSE(result.allow_trusted_address_checks);
  }

  // Bad IP in the header.
  {
    Http::TestRequestHeaderMapImpl headers{{"x-real-ip", "not-a-real-ip"}};
    Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = custom_header_extension_->detect(params);

    EXPECT_EQ(nullptr, result.detected_remote_address);
    EXPECT_FALSE(result.allow_trusted_address_checks);
  }

  // Good IP.
  {
    Http::TestRequestHeaderMapImpl headers{{"x-real-ip", "1.2.3.4"}};
    Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = custom_header_extension_->detect(params);

    EXPECT_EQ("1.2.3.4:0", result.detected_remote_address->asString());
    EXPECT_TRUE(result.allow_trusted_address_checks);
  }
}

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Extensions
} // namespace Envoy
