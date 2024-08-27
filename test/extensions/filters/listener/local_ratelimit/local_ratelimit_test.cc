#include <memory>

#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listener_filter_buffer_impl.h"
#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/listener/local_ratelimit/local_ratelimit.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"

#include "absl/strings/str_format.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::ByMove;
using testing::InSequence;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace LocalRateLimit {
namespace {

class LocalRateLimitTest : public testing::Test, public Event::TestUsingSimulatedTime {
public:
  class ActiveFilter {
  public:
    ActiveFilter(const FilterConfigSharedPtr& config) : filter_(config) {
      EXPECT_EQ(filter_.maxReadBytes(), 0);
      ON_CALL(cb_, socket()).WillByDefault(ReturnRef(socket_));
      ON_CALL(socket_, ioHandle()).WillByDefault(ReturnRef(io_handle_));
    }

    Network::FilterStatus onAccept() { return filter_.onAccept(cb_); }
    Network::FilterStatus onEmptyData() {
      // This is for test coverage purpose only.
      // onData shouldn't be called since maxReadBytes is zero.
      Network::ListenerFilterBufferImpl buffer(
          io_handle_, dispatcher_, [](bool) {}, [](Network::ListenerFilterBuffer&) {}, false, 1);
      return filter_.onData(buffer);
    }

    void expectIoHandleClose() {
      EXPECT_CALL(io_handle_, close()).WillOnce(Return(ByMove(Api::ioCallUint64ResultNoError())));
    }

  private:
    Filter filter_;
    NiceMock<Event::MockDispatcher> dispatcher_;
    NiceMock<Network::MockListenerFilterCallbacks> cb_;
    NiceMock<Network::MockConnectionSocket> socket_;
    NiceMock<Network::MockIoHandle> io_handle_;
  };

  uint64_t initialize(const std::string& filter_yaml) {
    envoy::extensions::filters::listener::local_ratelimit::v3::LocalRateLimit proto_config;
    TestUtility::loadFromYaml(filter_yaml, proto_config);
    config_ = std::make_shared<FilterConfig>(proto_config, dispatcher_, *stats_store_.rootScope(),
                                             runtime_);
    return proto_config.token_bucket().max_tokens();
  }

  NiceMock<Event::MockDispatcher> dispatcher_;
  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  FilterConfigSharedPtr config_;
};

// Basic no rate limit case.
TEST_F(LocalRateLimitTest, NoRateLimit) {
  initialize(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 1
  fill_interval: 1s
)EOF");

  InSequence s;
  ActiveFilter active_filter(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter.onAccept());
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter.onEmptyData());
  EXPECT_EQ(0, TestUtility::findCounter(
                   stats_store_, "listener_local_ratelimit.local_rate_limit_stats.rate_limited")
                   ->value());
}

// Basic rate limit case.
TEST_F(LocalRateLimitTest, RateLimit) {
  initialize(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 1
  fill_interval: 3s
)EOF");

  InSequence s;
  ActiveFilter active_filter1(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.onAccept());

  ActiveFilter active_filter2(config_);
  active_filter2.expectIoHandleClose();
  EXPECT_EQ(Network::FilterStatus::StopIteration, active_filter2.onAccept());

  EXPECT_EQ(1, TestUtility::findCounter(
                   stats_store_, "listener_local_ratelimit.local_rate_limit_stats.rate_limited")
                   ->value());

  // Refill the bucket.
  dispatcher_.globalTimeSystem().advanceTimeWait(std::chrono::milliseconds(3000));

  // Third socket is allowed after refill.
  ActiveFilter active_filter3(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter3.onAccept());

  EXPECT_EQ(1, TestUtility::findCounter(
                   stats_store_, "listener_local_ratelimit.local_rate_limit_stats.rate_limited")
                   ->value());
}

// Verify the runtime disable functionality.
TEST_F(LocalRateLimitTest, RuntimeDisabled) {
  initialize(R"EOF(
stat_prefix: local_rate_limit_stats
token_bucket:
  max_tokens: 1
  fill_interval: 3s
runtime_enabled:
  default_value: true
  runtime_key: foo_key
)EOF");

  InSequence s;
  ActiveFilter active_filter1(config_);
  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo_key", true)).WillOnce(Return(true));
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.onAccept());

  // Second socket is allowed since we disable the local ratelimit.
  ActiveFilter active_filter2(config_);
  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo_key", true)).WillOnce(Return(false));
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter2.onAccept());

  EXPECT_EQ(0, TestUtility::findCounter(
                   stats_store_, "listener_local_ratelimit.local_rate_limit_stats.rate_limited")
                   ->value());

  // Third socket is blocked.
  ActiveFilter active_filter3(config_);
  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo_key", true)).WillOnce(Return(true));
  active_filter3.expectIoHandleClose();
  EXPECT_EQ(Network::FilterStatus::StopIteration, active_filter3.onAccept());

  EXPECT_EQ(1, TestUtility::findCounter(
                   stats_store_, "listener_local_ratelimit.local_rate_limit_stats.rate_limited")
                   ->value());
}

} // namespace
} // namespace LocalRateLimit
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
