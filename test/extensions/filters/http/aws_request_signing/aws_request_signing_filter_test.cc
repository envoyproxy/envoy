#include "extensions/common/aws/signer.h"
#include "extensions/filters/http/aws_request_signing/aws_request_signing_filter.h"

#include "test/extensions/common/aws/mocks.h"
#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {
namespace {

class MockFilterConfig : public FilterConfig {
public:
  MockFilterConfig() { signer_ = std::make_shared<Common::Aws::MockSigner>(); }

  Common::Aws::Signer& signer() override { return *signer_; }
  FilterStats& stats() override { return stats_; }
  const std::string& hostRewrite() const override { return host_rewrite_; }

  std::shared_ptr<Common::Aws::MockSigner> signer_;
  Stats::IsolatedStoreImpl stats_store_;
  FilterStats stats_{Filter::generateStats("test", stats_store_)};
  std::string host_rewrite_;
};

class AwsRequestSigningFilterTest : public testing::Test {
public:
  void setup() {
    filter_config_ = std::make_shared<MockFilterConfig>();
    filter_ = std::make_unique<Filter>(filter_config_);
  }

  std::shared_ptr<MockFilterConfig> filter_config_;
  std::unique_ptr<Filter> filter_;
};

// Verify filter functionality when signing works.
TEST_F(AwsRequestSigningFilterTest, SignSucceeds) {
  setup();
  EXPECT_CALL(*(filter_config_->signer_), sign(_)).Times(1);

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1UL, filter_config_->stats_.signing_added_.value());
}

// Verify filter functionality when a host rewrite happens.
TEST_F(AwsRequestSigningFilterTest, SignWithHostRewrite) {
  setup();
  filter_config_->host_rewrite_ = "foo";
  EXPECT_CALL(*(filter_config_->signer_), sign(_)).Times(1);

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ("foo", headers.Host()->value().getStringView());
  EXPECT_EQ(1UL, filter_config_->stats_.signing_added_.value());
}

// Verify filter functionality when signing fails.
TEST_F(AwsRequestSigningFilterTest, SignFails) {
  setup();
  EXPECT_CALL(*(filter_config_->signer_), sign(_)).WillOnce(Invoke([](Http::HeaderMap&) -> void {
    throw EnvoyException("failed");
  }));

  Http::TestRequestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1UL, filter_config_->stats_.signing_failed_.value());
}

} // namespace
} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
