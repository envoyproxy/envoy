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
    }
    config_ = std::make_shared<Config>(proto_config, dispatcher_, stats_store_, runtime_);
  }

  Thread::ThreadSynchronizer& synchronizer() { return config_->synchronizer_; }

  NiceMock<Event::MockDispatcher> dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  Event::MockTimer* fill_timer_{};
  ConfigSharedPtr config_;
};

// Make sure we fail with a fill rate this is too fast.
TEST_F(LocalRateLimitTestBase, TooFastFillRate) {
  EXPECT_THROW_WITH_MESSAGE(initialize(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 1
  fill_interval: 0.049s
)EOF",
                                       false),
                            EnvoyException,
                            "local rate limit token bucket fill timer must be >= 50ms");
}

// Verify various token bucket CAS edge cases.
TEST_F(LocalRateLimitTestBase, CasEdgeCases) {
  // This tests the case in which a connection creation races with the fill timer.
  {
    initialize(R"EOF(
  stat_prefix: local_rate_limit_stats
  token_bucket:
    max_tokens: 1
    fill_interval: 0.05s
  )EOF");

    synchronizer().enable();

    // Start a thread and start the fill callback. This will wait pre-CAS.
    synchronizer().waitOn("on_fill_timer_pre_cas");
    std::thread t1([&] {
      EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(50), nullptr));
      fill_timer_->invokeCallback();
    });
    // Wait until the thread is actually waiting.
    synchronizer().barrierOn("on_fill_timer_pre_cas");

    // Create a connection. This should succeed.
    EXPECT_TRUE(config_->canCreateConnection());

    // Now signal the thread to continue which should cause a CAS failure and the loop to repeat.
    synchronizer().signal("on_fill_timer_pre_cas");
    t1.join();

    // 1 -> 0 tokens
    EXPECT_TRUE(config_->canCreateConnection());
    EXPECT_FALSE(config_->canCreateConnection());
  }

  // This tests the case in which two connection creations race.
  {
    initialize(R"EOF(
  stat_prefix: local_rate_limit_stats
  token_bucket:
    max_tokens: 1
    fill_interval: 0.2s
  )EOF");

    synchronizer().enable();

    // Start a thread and see if we can create a connection. This will wait pre-CAS.
    synchronizer().waitOn("can_create_connection_pre_cas");
    std::thread t1([&] { EXPECT_FALSE(config_->canCreateConnection()); });
    // Wait until the thread is actually waiting.
    synchronizer().barrierOn("can_create_connection_pre_cas");

    // Create the connection on this thread, which should cause the CAS to fail on the other thread.
    EXPECT_TRUE(config_->canCreateConnection());
    synchronizer().signal("can_create_connection_pre_cas");
    t1.join();
  }
}

// Verify token bucket functionality with a single token.
TEST_F(LocalRateLimitTestBase, TokenBucket) {
  initialize(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 1
  fill_interval: 0.2s
)EOF");

  // 1 -> 0 tokens
  EXPECT_TRUE(config_->canCreateConnection());
  EXPECT_FALSE(config_->canCreateConnection());
  EXPECT_FALSE(config_->canCreateConnection());

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(config_->canCreateConnection());
  EXPECT_FALSE(config_->canCreateConnection());

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(config_->canCreateConnection());
  EXPECT_FALSE(config_->canCreateConnection());
}

// Verify token bucket functionality with max tokens and tokens per fill > 1.
TEST_F(LocalRateLimitTestBase, TokenBucketMultipleTokensPerFill) {
  initialize(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 2
  tokens_per_fill: 2
  fill_interval: 0.2s
)EOF");

  // 2 -> 0 tokens
  EXPECT_TRUE(config_->canCreateConnection());
  EXPECT_TRUE(config_->canCreateConnection());
  EXPECT_FALSE(config_->canCreateConnection());

  // 0 -> 2 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 2 -> 1 tokens
  EXPECT_TRUE(config_->canCreateConnection());

  // 1 -> 2 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 2 -> 0 tokens
  EXPECT_TRUE(config_->canCreateConnection());
  EXPECT_TRUE(config_->canCreateConnection());
  EXPECT_FALSE(config_->canCreateConnection());
}

// Verify token bucket functionality with max tokens > tokens per fill.
TEST_F(LocalRateLimitTestBase, TokenBucketMaxTokensGreaterThanTokensPerFill) {
  initialize(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 2
  tokens_per_fill: 1
  fill_interval: 0.2s
)EOF");

  // 2 -> 0 tokens
  EXPECT_TRUE(config_->canCreateConnection());
  EXPECT_TRUE(config_->canCreateConnection());
  EXPECT_FALSE(config_->canCreateConnection());

  // 0 -> 1 tokens
  EXPECT_CALL(*fill_timer_, enableTimer(std::chrono::milliseconds(200), nullptr));
  fill_timer_->invokeCallback();

  // 1 -> 0 tokens
  EXPECT_TRUE(config_->canCreateConnection());
  EXPECT_FALSE(config_->canCreateConnection());
}

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
