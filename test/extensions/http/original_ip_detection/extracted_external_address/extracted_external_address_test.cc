#include "envoy/extensions/http/original_ip_detection/extracted_external_address/v3/extracted_external_address.pb.h"

#include "source/common/network/utility.h"
#include "source/extensions/http/original_ip_detection/extracted_external_address/extracted_external_address.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Http {
namespace OriginalIPDetection {
namespace ExtractedExternalAddress {

class ExtractedExternalAddressTest : public testing::Test {
protected:
  ExtractedExternalAddressTest() {
    envoy::extensions::http::original_ip_detection::extracted_external_address::v3::
        ExtractedExternalAddressConfig config;
    extension_ = std::make_shared<ExtractedExternalAddressIPDetection>(config);
  }

  std::shared_ptr<ExtractedExternalAddressIPDetection> extension_;
};

TEST_F(ExtractedExternalAddressTest, HeaderAbsent) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{"x-other", "abc"}};
  Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
  auto result = extension_->detect(params);

  // Header absent: no detection result. Subsequent extensions in the chain
  // (or the default XFF fallback synthesized by the HCM) get a chance to run.
  EXPECT_EQ(nullptr, result.detected_remote_address);
  EXPECT_FALSE(result.allow_trusted_address_checks);
  EXPECT_FALSE(result.reject_options.has_value());
  EXPECT_TRUE(result.skip_xff_append);
}

TEST_F(ExtractedExternalAddressTest, HeaderUnparseable) {
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-external-address", "not-a-real-ip"}};
  Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
  auto result = extension_->detect(params);

  // Unparseable header: also fall through to subsequent extensions rather
  // than rejecting. Matches the no-detection branch contract.
  EXPECT_EQ(nullptr, result.detected_remote_address);
  EXPECT_FALSE(result.allow_trusted_address_checks);
  EXPECT_FALSE(result.reject_options.has_value());
  EXPECT_TRUE(result.skip_xff_append);
}

TEST_F(ExtractedExternalAddressTest, HeaderEmpty) {
  Envoy::Http::TestRequestHeaderMapImpl headers{{"x-envoy-external-address", ""}};
  Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
  auto result = extension_->detect(params);

  EXPECT_EQ(nullptr, result.detected_remote_address);
  EXPECT_FALSE(result.allow_trusted_address_checks);
  EXPECT_FALSE(result.reject_options.has_value());
  EXPECT_TRUE(result.skip_xff_append);
}

TEST_F(ExtractedExternalAddressTest, MultiValueHeader) {
  // Two ``x-envoy-external-address`` headers in the same request: the inline
  // accessor returns a single comma-joined value, which fails IP parse and
  // falls through to the chain. This is unusual but possible in HTTP/1.1.
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-external-address", "1.2.3.4"},
      {"x-envoy-external-address", "5.6.7.8"}};
  Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
  auto result = extension_->detect(params);

  EXPECT_EQ(nullptr, result.detected_remote_address);
  EXPECT_FALSE(result.allow_trusted_address_checks);
  EXPECT_FALSE(result.reject_options.has_value());
  EXPECT_TRUE(result.skip_xff_append);
}

TEST_F(ExtractedExternalAddressTest, IPv4) {
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-external-address", "203.0.113.50"}};
  Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
  auto result = extension_->detect(params);

  EXPECT_EQ("203.0.113.50:0", result.detected_remote_address->asString());
  EXPECT_TRUE(result.allow_trusted_address_checks);
  EXPECT_FALSE(result.reject_options.has_value());
  EXPECT_TRUE(result.skip_xff_append);
}

TEST_F(ExtractedExternalAddressTest, IPv6) {
  Envoy::Http::TestRequestHeaderMapImpl headers{
      {"x-envoy-external-address", "2001:db8::1"}};
  Envoy::Http::OriginalIPDetectionParams params = {headers, nullptr};
  auto result = extension_->detect(params);

  EXPECT_EQ("[2001:db8::1]:0", result.detected_remote_address->asString());
  EXPECT_TRUE(result.allow_trusted_address_checks);
  EXPECT_FALSE(result.reject_options.has_value());
  EXPECT_TRUE(result.skip_xff_append);
}

} // namespace ExtractedExternalAddress
} // namespace OriginalIPDetection
} // namespace Http
} // namespace Extensions
} // namespace Envoy
