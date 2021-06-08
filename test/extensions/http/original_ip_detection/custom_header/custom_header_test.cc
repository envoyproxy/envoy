#include "envoy/extensions/http/original_ip_detection/custom_header/v3/custom_header.pb.h"

#include "source/common/network/utility.h"
#include "source/extensions/http/original_ip_detection/custom_header/custom_header.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace CustomHeader {

class CustomHeaderTest : public testing::Test {
protected:
  CustomHeaderTest() { configure(); }

  void configure(envoy::type::v3::StatusCode code = envoy::type::v3::StatusCode::Unauthorized) {
    envoy::extensions::http::original_ip_detection::custom_header::v3::CustomHeaderConfig config;
    config.set_header_name("x-real-ip");
    config.set_allow_extension_to_set_address_as_trusted(true);
    auto* reject_with_status = config.mutable_reject_with_status();
    reject_with_status->set_code(code);
    custom_header_extension_ = std::make_shared<CustomHeaderIPDetection>(config);
  }

  std::shared_ptr<CustomHeaderIPDetection> custom_header_extension_;
};

TEST_F(CustomHeaderTest, Detection) {
  // Header missing.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers{{"x-other", "abc"}};
    Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = custom_header_extension_->detect(params);

    EXPECT_EQ(nullptr, result.detected_remote_address);
    EXPECT_FALSE(result.allow_trusted_address_checks);
    EXPECT_TRUE(result.reject_options.has_value());

    const auto& reject_options = result.reject_options.value();
    EXPECT_EQ(reject_options.response_code, Envoy::Http::Code::Unauthorized);
    EXPECT_EQ(reject_options.body, "");
  }

  // Bad IP in the header.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers{{"x-real-ip", "not-a-real-ip"}};
    Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = custom_header_extension_->detect(params);

    EXPECT_EQ(nullptr, result.detected_remote_address);
    EXPECT_FALSE(result.allow_trusted_address_checks);
    EXPECT_TRUE(result.reject_options.has_value());

    const auto& reject_options = result.reject_options.value();
    EXPECT_EQ(reject_options.response_code, Envoy::Http::Code::Unauthorized);
    EXPECT_EQ(reject_options.body, "");
  }

  // Good IPv4.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers{{"x-real-ip", "1.2.3.4"}};
    Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = custom_header_extension_->detect(params);

    EXPECT_EQ("1.2.3.4:0", result.detected_remote_address->asString());
    EXPECT_TRUE(result.allow_trusted_address_checks);
    EXPECT_FALSE(result.reject_options.has_value());
  }

  // Good IPv6.
  {
    Envoy::Http::TestRequestHeaderMapImpl headers{{"x-real-ip", "fc00::1"}};
    Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
    auto result = custom_header_extension_->detect(params);

    EXPECT_EQ("[fc00::1]:0", result.detected_remote_address->asString());
    EXPECT_TRUE(result.allow_trusted_address_checks);
    EXPECT_FALSE(result.reject_options.has_value());
  }
}

TEST_F(CustomHeaderTest, FallbacksToDefaultResponseCode) {
  configure(envoy::type::v3::StatusCode::OK);

  Envoy::Http::TestRequestHeaderMapImpl headers{{"x-other", "abc"}};
  Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
  auto result = custom_header_extension_->detect(params);

  EXPECT_EQ(nullptr, result.detected_remote_address);
  EXPECT_FALSE(result.allow_trusted_address_checks);
  EXPECT_TRUE(result.reject_options.has_value());

  const auto& reject_options = result.reject_options.value();
  EXPECT_EQ(reject_options.response_code, Envoy::Http::Code::Forbidden);
  EXPECT_EQ(reject_options.body, "");
}

} // namespace CustomHeader
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
