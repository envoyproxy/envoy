#include "envoy/extensions/filters/network/connection_limit/v3/connection_limit.pb.h"
#include "envoy/extensions/filters/network/connection_limit/v3/connection_limit.pb.validate.h"

#include "source/common/stats/isolated_store_impl.h"
#include "source/extensions/filters/network/connection_limit/connection_limit.h"

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
namespace ConnectionLimitFilter {

class ConnectionLimitTestBase : public testing::Test {
public:
  void initialize(const std::string& filter_yaml) {
    envoy::extensions::filters::network::connection_limit::v3::ConnectionLimit proto_config;
    TestUtility::loadFromYamlAndValidate(filter_yaml, proto_config);
    config_ = std::make_shared<Config>(proto_config, *stats_store_.rootScope(), runtime_);
  }

  Thread::ThreadSynchronizer& synchronizer() { return config_->synchronizer_; }

  Stats::IsolatedStoreImpl stats_store_;
  NiceMock<Runtime::MockLoader> runtime_;
  ConfigSharedPtr config_;
};

class ConnectionLimitFilterTest : public ConnectionLimitTestBase {
public:
  struct ActiveFilter {
    ActiveFilter(const ConfigSharedPtr& config) : filter_(config) {
      filter_.initializeReadFilterCallbacks(read_filter_callbacks_);
    }

    NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
    Filter filter_;
  };
};

// Basic no connection limit case.
TEST_F(ConnectionLimitFilterTest, NoConnectionLimit) {
  initialize(R"EOF(
stat_prefix: connection_limit_stats
max_connections: 1
delay: 0.2s
)EOF");

  InSequence s;
  Buffer::OwnedImpl buffer("test");
  ActiveFilter active_filter(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter.filter_.onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter.filter_.onData(buffer, false));
  EXPECT_EQ(1, TestUtility::findGauge(stats_store_,
                                      "connection_limit.connection_limit_stats.active_connections")
                   ->value());
  EXPECT_EQ(0, TestUtility::findCounter(
                   stats_store_, "connection_limit.connection_limit_stats.limited_connections")
                   ->value());
}

// Basic connection limit case.
TEST_F(ConnectionLimitFilterTest, ConnectionLimit) {
  initialize(R"EOF(
stat_prefix: connection_limit_stats
max_connections: 2
delay: 0s
)EOF");

  // First connection is OK.
  InSequence s;
  Buffer::OwnedImpl buffer("test");
  ActiveFilter active_filter1(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.filter_.onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.filter_.onData(buffer, false));

  // Second connection is OK.
  ActiveFilter active_filter2(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter2.filter_.onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter2.filter_.onData(buffer, false));
  EXPECT_EQ(2, TestUtility::findGauge(stats_store_,
                                      "connection_limit.connection_limit_stats.active_connections")
                   ->value());

  // Third connection should be connection limited.
  ActiveFilter active_filter3(config_);
  EXPECT_CALL(active_filter3.read_filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "over_connection_limit"));
  EXPECT_EQ(Network::FilterStatus::StopIteration, active_filter3.filter_.onNewConnection());
  EXPECT_EQ(Network::FilterStatus::StopIteration, active_filter3.filter_.onData(buffer, false));
  EXPECT_EQ(1, TestUtility::findCounter(
                   stats_store_, "connection_limit.connection_limit_stats.limited_connections")
                   ->value());
  EXPECT_EQ(2, TestUtility::findGauge(stats_store_,
                                      "connection_limit.connection_limit_stats.active_connections")
                   ->value());
}

// Connection limit with delay case.
TEST_F(ConnectionLimitFilterTest, ConnectionLimitWithDelay) {
  initialize(R"EOF(
stat_prefix: connection_limit_stats
max_connections: 1
delay: 0.2s
)EOF");

  // First connection is OK.
  InSequence s;
  Buffer::OwnedImpl buffer("test");
  ActiveFilter active_filter1(config_);
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.filter_.onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.filter_.onData(buffer, false));

  // Second connection should be connection limited.
  ActiveFilter active_filter2(config_);
  Event::MockTimer* delay_timer = new NiceMock<Event::MockTimer>(
      &active_filter2.read_filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(*delay_timer, enableTimer(std::chrono::milliseconds(200), _));
  EXPECT_EQ(Network::FilterStatus::StopIteration, active_filter2.filter_.onNewConnection());
  EXPECT_EQ(Network::FilterStatus::StopIteration, active_filter2.filter_.onData(buffer, false));
  EXPECT_EQ(1, TestUtility::findCounter(
                   stats_store_, "connection_limit.connection_limit_stats.limited_connections")
                   ->value());
  EXPECT_EQ(2, TestUtility::findGauge(stats_store_,
                                      "connection_limit.connection_limit_stats.active_connections")
                   ->value());
  EXPECT_CALL(active_filter2.read_filter_callbacks_.connection_,
              close(Network::ConnectionCloseType::NoFlush, "over_connection_limit"));
  delay_timer->invokeCallback();
  EXPECT_EQ(1, TestUtility::findGauge(stats_store_,
                                      "connection_limit.connection_limit_stats.active_connections")
                   ->value());
}

// Verify the runtime disable functionality.
TEST_F(ConnectionLimitFilterTest, RuntimeDisabled) {
  initialize(R"EOF(
stat_prefix: connection_limit_stats
max_connections: 1
delay: 0.2s
runtime_enabled:
  default_value: true
  runtime_key: foo_key
)EOF");

  // First connection is OK.
  InSequence s;
  Buffer::OwnedImpl buffer("test");
  ActiveFilter active_filter1(config_);
  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo_key", true)).WillOnce(Return(true));
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.filter_.onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter1.filter_.onData(buffer, false));

  // Second connection should be connection limited but won't be due to filter disable.
  ActiveFilter active_filter2(config_);
  EXPECT_CALL(runtime_.snapshot_, getBoolean("foo_key", true)).WillOnce(Return(false));
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter2.filter_.onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter2.filter_.onData(buffer, false));
  EXPECT_EQ(1, TestUtility::findGauge(stats_store_,
                                      "connection_limit.connection_limit_stats.active_connections")
                   ->value());
  EXPECT_EQ(0, TestUtility::findCounter(
                   stats_store_, "connection_limit.connection_limit_stats.limited_connections")
                   ->value());
}

// Verify increment connection counter CAS edge case.
TEST_F(ConnectionLimitFilterTest, IncrementCasEdgeCase) {
  initialize(R"EOF(
stat_prefix: connection_limit_stats
max_connections: 1
delay: 0s
)EOF");

  InSequence s;
  Buffer::OwnedImpl buffer("test");
  ActiveFilter active_filter(config_);

  synchronizer().enable();

  // Start a thread and see if we are under limit. This will wait pre-CAS.
  synchronizer().waitOn("increment_pre_cas");
  std::thread t1([&] {
    EXPECT_EQ(Network::FilterStatus::StopIteration, active_filter.filter_.onNewConnection());
    EXPECT_EQ(Network::FilterStatus::StopIteration, active_filter.filter_.onData(buffer, false));
  });
  // Wait until the thread is actually waiting.
  synchronizer().barrierOn("increment_pre_cas");

  // Increase connection counter to 1, which should cause the CAS to fail on the other thread.
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter.filter_.onNewConnection());
  EXPECT_EQ(Network::FilterStatus::Continue, active_filter.filter_.onData(buffer, false));
  synchronizer().signal("increment_pre_cas");
  t1.join();
}

} // namespace ConnectionLimitFilter
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
