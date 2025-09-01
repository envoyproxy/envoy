#include <string>
#include <utility>
#include <vector>

#include "envoy/config/core/v3/http_service.pb.h"
#include "source/extensions/tracers/zipkin/zipkin_tracer_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {
namespace {

TEST(CollectorInfoTest, DefaultConstruction) {
  CollectorInfo collector_info;

  // Default values should be set correctly
  EXPECT_TRUE(collector_info.endpoint_.empty());
  EXPECT_TRUE(collector_info.hostname_.empty());
  EXPECT_EQ(collector_info.version_, envoy::config::trace::v3::ZipkinConfig::HTTP_JSON);
  EXPECT_TRUE(collector_info.shared_span_context_);
  EXPECT_TRUE(collector_info.request_headers_.empty());
  
  // New fields should have default values
  EXPECT_TRUE(collector_info.use_legacy_config_);  // Defaults to legacy mode
  EXPECT_FALSE(collector_info.http_service_.has_value());
}

TEST(CollectorInfoTest, CustomHeadersAssignment) {
  CollectorInfo collector_info;

  // Add custom headers
  collector_info.request_headers_ = {{"Authorization", "Bearer token123"},
                                     {"X-Custom-Header", "custom-value"},
                                     {"X-API-Key", "api-key-123"}};

  // Verify headers were set correctly
  EXPECT_EQ(collector_info.request_headers_.size(), 3);
  EXPECT_EQ(collector_info.request_headers_[0].first, "Authorization");
  EXPECT_EQ(collector_info.request_headers_[0].second, "Bearer token123");
  EXPECT_EQ(collector_info.request_headers_[1].first, "X-Custom-Header");
  EXPECT_EQ(collector_info.request_headers_[1].second, "custom-value");
  EXPECT_EQ(collector_info.request_headers_[2].first, "X-API-Key");
  EXPECT_EQ(collector_info.request_headers_[2].second, "api-key-123");
}

TEST(CollectorInfoTest, EmptyCustomHeaders) {
  CollectorInfo collector_info;

  // Explicitly set empty headers
  collector_info.request_headers_ = {};

  // Verify headers are empty
  EXPECT_TRUE(collector_info.request_headers_.empty());
  EXPECT_EQ(collector_info.request_headers_.size(), 0);
}

TEST(CollectorInfoTest, CustomHeadersWithCompleteConfiguration) {
  CollectorInfo collector_info;

  // Set all fields including custom headers
  collector_info.endpoint_ = "/api/v2/spans";
  collector_info.hostname_ = "zipkin.example.com";
  collector_info.version_ = envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO;
  collector_info.shared_span_context_ = false;
  collector_info.request_headers_ = {{"Content-Type", "application/x-protobuf"},
                                     {"Authorization", "Bearer secret-token"},
                                     {"X-Zipkin-Trace", "enabled"}};

  // Verify all fields are set correctly
  EXPECT_EQ(collector_info.endpoint_, "/api/v2/spans");
  EXPECT_EQ(collector_info.hostname_, "zipkin.example.com");
  EXPECT_EQ(collector_info.version_, envoy::config::trace::v3::ZipkinConfig::HTTP_PROTO);
  EXPECT_FALSE(collector_info.shared_span_context_);
  EXPECT_EQ(collector_info.request_headers_.size(), 3);

  // Verify specific headers
  EXPECT_EQ(collector_info.request_headers_[0].first, "Content-Type");
  EXPECT_EQ(collector_info.request_headers_[0].second, "application/x-protobuf");
  EXPECT_EQ(collector_info.request_headers_[1].first, "Authorization");
  EXPECT_EQ(collector_info.request_headers_[1].second, "Bearer secret-token");
  EXPECT_EQ(collector_info.request_headers_[2].first, "X-Zipkin-Trace");
  EXPECT_EQ(collector_info.request_headers_[2].second, "enabled");
}

TEST(CollectorInfoTest, SingleCustomHeader) {
  CollectorInfo collector_info;

  // Add single custom header
  collector_info.request_headers_ = {{"X-Single-Header", "single-value"}};

  // Verify single header was set correctly
  EXPECT_EQ(collector_info.request_headers_.size(), 1);
  EXPECT_EQ(collector_info.request_headers_[0].first, "X-Single-Header");
  EXPECT_EQ(collector_info.request_headers_[0].second, "single-value");
}

TEST(CollectorInfoTest, LegacyConfigurationMode) {
  CollectorInfo collector_info;

  // Set up legacy configuration
  collector_info.use_legacy_config_ = true;
  collector_info.endpoint_ = "/api/v2/spans";
  collector_info.hostname_ = "zipkin.legacy.com";

  // Verify legacy configuration
  EXPECT_TRUE(collector_info.use_legacy_config_);
  EXPECT_EQ(collector_info.endpoint_, "/api/v2/spans");
  EXPECT_EQ(collector_info.hostname_, "zipkin.legacy.com");
  EXPECT_FALSE(collector_info.http_service_.has_value());
}

TEST(CollectorInfoTest, HttpServiceConfigurationMode) {
  CollectorInfo collector_info;

  // Create mock HttpService configuration
  envoy::config::core::v3::HttpService http_service;
  auto* http_uri = http_service.mutable_http_uri();
  http_uri->set_uri("/api/v2/spans");
  http_uri->set_cluster("zipkin_collector");
  http_uri->mutable_timeout()->set_seconds(5);
  
  // Set up HttpService configuration
  collector_info.use_legacy_config_ = false;
  collector_info.http_service_ = http_service;
  collector_info.endpoint_ = "/api/v2/spans";  // Should be populated from HttpService
  collector_info.hostname_ = "zipkin_collector";  // Should be populated from cluster name

  // Verify HttpService configuration
  EXPECT_FALSE(collector_info.use_legacy_config_);
  EXPECT_TRUE(collector_info.http_service_.has_value());
  EXPECT_EQ(collector_info.http_service_->http_uri().uri(), "/api/v2/spans");
  EXPECT_EQ(collector_info.http_service_->http_uri().cluster(), "zipkin_collector");
  EXPECT_EQ(collector_info.endpoint_, "/api/v2/spans");  // Should match HttpService URI
  EXPECT_EQ(collector_info.hostname_, "zipkin_collector");  // Should match cluster name
}

} // namespace
} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
