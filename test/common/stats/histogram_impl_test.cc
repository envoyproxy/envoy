#include "envoy/config/metrics/v3/stats.pb.h"

#include "common/stats/histogram_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Stats {

class HistogramSettingsImplTest : public testing::Test {
public:
  void initialize() {
    envoy::config::metrics::v3::StatsConfig config;
    auto& bucket_settings = *config.mutable_histogram_bucket_settings();
    for (auto& item : buckets_configs_) {
      bucket_settings.Add(std::move(item));
    }
    settings_ = std::make_unique<HistogramSettingsImpl>(config);
  }

  std::vector<envoy::config::metrics::v3::HistogramBucketSettings> buckets_configs_;
  std::unique_ptr<HistogramSettingsImpl> settings_;
};

// Test that a matching stat returns the configured buckets, and a non-matching
// stat returns the defaults.
TEST_F(HistogramSettingsImplTest, Basic) {
  envoy::config::metrics::v3::HistogramBucketSettings setting;
  setting.mutable_match()->set_prefix("a");
  setting.mutable_buckets()->Add(0.1);
  setting.mutable_buckets()->Add(2);
  buckets_configs_.push_back(setting);

  initialize();
  EXPECT_EQ(settings_->buckets("test"), settings_->defaultBuckets());
  EXPECT_EQ(settings_->buckets("abcd"), ConstSupportedBuckets({0.1, 2}));
}

// Test that only matching configurations are applied.
TEST_F(HistogramSettingsImplTest, Matching) {
  {
    envoy::config::metrics::v3::HistogramBucketSettings setting;
    setting.mutable_match()->set_prefix("a");
    setting.mutable_buckets()->Add(1);
    setting.mutable_buckets()->Add(2);
    buckets_configs_.push_back(setting);
  }

  {
    envoy::config::metrics::v3::HistogramBucketSettings setting;
    setting.mutable_match()->set_prefix("b");
    setting.mutable_buckets()->Add(3);
    setting.mutable_buckets()->Add(4);
    buckets_configs_.push_back(setting);
  }

  initialize();
  EXPECT_EQ(settings_->buckets("abcd"), ConstSupportedBuckets({1, 2}));
  EXPECT_EQ(settings_->buckets("bcde"), ConstSupportedBuckets({3, 4}));
}

// Test that earlier configs take precedence over later configs when both match.
TEST_F(HistogramSettingsImplTest, Priority) {
  {
    envoy::config::metrics::v3::HistogramBucketSettings setting;
    setting.mutable_match()->set_prefix("a");
    setting.mutable_buckets()->Add(1);
    setting.mutable_buckets()->Add(2);
    buckets_configs_.push_back(setting);
  }

  {
    envoy::config::metrics::v3::HistogramBucketSettings setting;
    setting.mutable_match()->set_prefix("ab");
    setting.mutable_buckets()->Add(3);
    setting.mutable_buckets()->Add(4);
  }

  initialize();
  EXPECT_EQ(settings_->buckets("abcd"), ConstSupportedBuckets({1, 2}));
}

} // namespace Stats
} // namespace Envoy
