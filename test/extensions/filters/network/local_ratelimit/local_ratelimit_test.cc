#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.h"
#include "envoy/extensions/filters/network/local_ratelimit/v3/local_rate_limit.pb.validate.h"

#include "source/common/singleton/manager_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/network/local_ratelimit/local_ratelimit.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::InSequence;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace LocalRateLimitFilter {

class LocalRateLimitTestBase : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  LocalRateLimitTestBase() = default;

  uint64_t initialize(const std::string& filter_yaml) {
    envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit proto_config;
    TestUtility::loadFromYamlAndValidate(filter_yaml, proto_config);
    config_ = std::make_shared<Config>(proto_config, dispatcher_, *stats_store_.rootScope(),
                                       runtime_, singleton_manager_);
    return proto_config.token_bucket().max_tokens();
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  Singleton::ManagerImpl singleton_manager_;
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
  EXPECT_CALL(active_filter2.read_filter_callbacks_.connection_.stream_info_,
              setResponseFlag(StreamInfo::CoreResponseFlag::UpstreamRetryLimitExceeded));
  EXPECT_CALL(active_filter2.read_filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "local_ratelimit_close_over_limit"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, active_filter2.filter_.onNewConnection());
  EXPECT_EQ(1, TestUtility::findCounter(stats_store_,
                                        "local_rate_limit.local_rate_limit_stats.rate_limited")
                   ->value());

  // Refill the bucket.
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(200));

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

class LocalRateLimitSharedTokenBucketTest : public LocalRateLimitTestBase {
public:
  void test(bool expect_sharing, const std::string& filter_yaml1, const std::string& filter_yaml2) {
    const uint64_t config1_tokens = initialize(filter_yaml1);

    envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit proto_config;
    TestUtility::loadFromYamlAndValidate(filter_yaml2, proto_config);
    const uint64_t config2_tokens = proto_config.token_bucket().max_tokens();
    config2_ = std::make_shared<Config>(proto_config, dispatcher_, *stats_store_.rootScope(),
                                        runtime_, singleton_manager_);

    // This test just uses the initial tokens without ever refilling.
    if (expect_sharing) {
      ASSERT_EQ(config1_tokens, config2_tokens);
      for (auto remaining_tokens = config1_tokens; remaining_tokens > 0; remaining_tokens--) {
        // Alternate configs for each iteration.
        auto& config = (remaining_tokens & 1) ? config_ : config2_;
        EXPECT_TRUE(config->canCreateConnection());
      }
      EXPECT_FALSE(config_->canCreateConnection());
      EXPECT_FALSE(config2_->canCreateConnection());
    } else {
      for (auto& config :
           {std::make_pair(config_, config1_tokens), std::make_pair(config2_, config2_tokens)}) {
        for (auto remaining_tokens = config.second; remaining_tokens > 0; remaining_tokens--) {
          EXPECT_TRUE(config.first->canCreateConnection());
        }
        EXPECT_FALSE(config.first->canCreateConnection());
      }
    }
  }

  const char* yaml_no_shared_key = R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 2
  fill_interval: 1s
)EOF";

  const char* yaml_shared_key_1 = R"EOF(
stat_prefix: local_rate_limit_stats
share_key: a
token_bucket:
  max_tokens: 2
  fill_interval: 1s
)EOF";

  const char* yaml_shared_key_1_different_token_settings = R"EOF(
stat_prefix: local_rate_limit_stats
share_key: a
token_bucket:
  max_tokens: 5
  fill_interval: 1s
)EOF";

  const char* yaml_shared_key_2 = R"EOF(
stat_prefix: local_rate_limit_stats
share_key: b
token_bucket:
  max_tokens: 2
  fill_interval: 1s
)EOF";

  ConfigSharedPtr config2_;
};

TEST_F(LocalRateLimitSharedTokenBucketTest, EmptyKeyMatchingConfig) {
  test(false, yaml_no_shared_key, yaml_no_shared_key);
}

TEST_F(LocalRateLimitSharedTokenBucketTest, MatchingKeyMismatchConfig) {
  test(false, yaml_shared_key_1, yaml_shared_key_1_different_token_settings);
}

TEST_F(LocalRateLimitSharedTokenBucketTest, MismatchKey) {
  test(false, yaml_shared_key_1, yaml_shared_key_2);
}

TEST_F(LocalRateLimitSharedTokenBucketTest, Shared) {
  test(true, yaml_shared_key_1, yaml_shared_key_1);
}

// Test that the Key from SharedRateLimitSingleton::get() is valid/stable even if
// many entries are added and the hash table is rehashed.
TEST_F(LocalRateLimitSharedTokenBucketTest, RehashPointerStability) {
  constexpr absl::string_view yaml_template = R"EOF(
stat_prefix: local_rate_limit_stats
share_key: key_{}
token_bucket:
  max_tokens: 2
  fill_interval: 1s
)EOF";

  std::vector<std::unique_ptr<Config>> configs;
  // This assumes that growing a hash table from size zero to size 100 will cause a rehash
  // at some point.
  for (uint32_t i = 0; i < 100; i++) {
    std::string yaml = fmt::format(yaml_template, i);
    envoy::extensions::filters::network::local_ratelimit::v3::LocalRateLimit proto_config;
    TestUtility::loadFromYamlAndValidate(yaml, proto_config);
    configs.push_back(std::make_unique<Config>(proto_config, dispatcher_, *stats_store_.rootScope(),
                                               runtime_, singleton_manager_));
  }

  configs.clear();
}

} // namespace LocalRateLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
