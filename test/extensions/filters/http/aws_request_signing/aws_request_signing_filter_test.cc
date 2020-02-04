#include "extensions/common/aws/signer.h"
#include "extensions/filters/http/aws_request_signing/aws_request_signing_filter.h"

#include "test/mocks/http/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AwsRequestSigningFilter {
namespace {

class MockSigner : public Common::Aws::Signer {
public:
  MOCK_METHOD(void, sign, (Http::HeaderMap&));
  MOCK_METHOD(void, sign, (Http::Message&, bool));
};

class MockFilterConfig : public FilterConfig {
public:
  MockFilterConfig() { signer_ = std::make_shared<MockSigner>(); }

  Common::Aws::Signer& signer() override { return *signer_; }
  FilterStats& stats() override { return stats_; }

  std::shared_ptr<MockSigner> signer_;
  Stats::IsolatedStoreImpl stats_store_;
  FilterStats stats_{Filter::generateStats("test", stats_store_)};
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

  Http::TestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1UL, filter_config_->stats_.signing_added_.value());
}

// Verify filter functionality when signing fails.
TEST_F(AwsRequestSigningFilterTest, SignFails) {
  setup();
  EXPECT_CALL(*(filter_config_->signer_), sign(_)).WillOnce(Invoke([](Http::HeaderMap&) -> void {
    throw EnvoyException("failed");
  }));

  Http::TestHeaderMapImpl headers;
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(headers, false));
  EXPECT_EQ(1UL, filter_config_->stats_.signing_failed_.value());
}

} // namespace
} // namespace AwsRequestSigningFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
