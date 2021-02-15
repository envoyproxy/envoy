#include "envoy/extensions/original_ip_detection/custom_header/v3/custom_header.pb.h"

#include "common/network/utility.h"

#include "extensions/original_ip_detection/custom_header/custom_header.h"

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

    auto* common_config = config.mutable_common_config();
    common_config->set_allow_trusted_address_checks(true);
    common_config->set_reject_request_if_detection_fails(true);
    common_config->set_body_on_error("detection failed");
    common_config->set_details_on_error("rejecting because detection failed");

    auto* status_on_error = common_config->mutable_status_on_error();
    status_on_error->set_code(envoy::type::v3::StatusCode::Unauthorized);

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
    EXPECT_TRUE(result.reject_options.has_value());

    const auto& reject_options = result.reject_options.value();
    EXPECT_EQ(reject_options.response_code, Http::Code::Unauthorized);
    EXPECT_EQ(reject_options.body, "detection failed");
    EXPECT_EQ(reject_options.details, "rejecting because detection failed");
  }

  // Bad IP in the header.
  {
    Http::TestRequestHeaderMapImpl headers{{"x-real-ip", "not-a-real-ip"}};
    Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = custom_header_extension_->detect(params);

    EXPECT_EQ(nullptr, result.detected_remote_address);
    EXPECT_FALSE(result.allow_trusted_address_checks);
    EXPECT_TRUE(result.reject_options.has_value());

    const auto& reject_options = result.reject_options.value();
    EXPECT_EQ(reject_options.response_code, Http::Code::Unauthorized);
    EXPECT_EQ(reject_options.body, "detection failed");
    EXPECT_EQ(reject_options.details, "rejecting because detection failed");
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
