#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.validate.h"

#include "common/stats/isolated_store_impl.h"

#include "extensions/filters/network/local_ratelimit/local_ratelimit.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LocalRateLimitFilter {

class LocalRateLimitTestBase : public testing::Test {
public:
  void initialize(const std::string& filter_yaml, bool expect_timer_create = true) {
    envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit proto_config;
    TestUtility::loadFromYamlAndValidate(filter_yaml, proto_config);
    fill_timer_ = new Event::MockTimer(&dispatcher_);
    if (expect_timer_create) {
      EXPECT_CALL(*fill_timer_, enableTimer(_, nullptr));
      EXPECT_CALL(*fill_timer_, disableTimer());
    }
    config_ = std::make_shared<Config>(proto_config, dispatcher_, stats_store_, runtime_);
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  Event::MockTimer* fill_timer_{};
  ConfigSharedPtr config_;
};

class LocalRateLimitFilterTest : public LocalRateLimitTestBase {
public:
  struct ActiveFilter {
    ActiveFilter(const ConfigSharedPtr& config) : filter_(config) {
      filter_.initializeReadFilterCallbacks(read_filter_callbacks_);
    }

    NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
    Filter filter_;
  };
};

// Basic no rate limit case.
TEST_F(LocalRateLimitFilterTest, NoRateLimit) {
  initialize(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 1
  fill_interval: 0.2s
)EOF");

  InSequence s;
  ActiveFilter active_filter(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter.filter_.onNewConnection());
  EXPECT_EQ(0, TestUtility::findCounter(stats_store_,
                                        "local_rate_limit.local_rate_limit_stats.rate_limited")
                   ->value());
}

// Basic rate limit case.
TEST_F(LocalRateLimitFilterTest, RateLimit) {
  initialize(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 1
  fill_interval: 0.2s
)EOF");

  // First connection is OK.
  InSequence s;
  ActiveFilter active_filter1(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.filter_.onNewConnection());

  // Second connection should be rate limited.
  ActiveFilter active_filter2(config_);
  EXPECT_CALL(active_filter2.read_filter_callbacks_.connection_, close(_));
  EXPECT_EQ(Network::FilterStatus::StopIteration, active_filter2.filter_.onNewConnection());
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_,
                                        "local_rate_limit.local_rate_limit_stats.rate_limited")
                   ->value());

  // Refill the bucket.
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // Third connection is OK.
  ActiveFilter active_filter3(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter3.filter_.onNewConnection());
}

// Verify the runtime disable functionality.
TEST_F(LocalRateLimitFilterTest, RuntimeDisabled) {
  initialize(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 1
  fill_interval: 0.2s
runtime_enabled:
  default_value: true
  runtime_key: foo_key
)EOF");

  // First connection is OK.
  InSequence s;
  ActiveFilter active_filter1(config_);
  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo_key", true)).WillOnce(Return(true));
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.filter_.onNewConnection());

  // Second connection should be rate limited but won't be due to filter disable.
  ActiveFilter active_filter2(config_);
  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo_key", true)).WillOnce(Return(false));
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter2.filter_.onNewConnection());
  EXPECT_EQ(0, TestUtility::findCounter(stats_store_,
                                        "local_rate_limit.local_rate_limit_stats.rate_limited")
                   ->value());
}

} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
