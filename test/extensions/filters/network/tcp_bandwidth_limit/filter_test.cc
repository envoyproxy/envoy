#include "source/extensions/filters/network/tcp_bandwidth_limit/tcp_bandwidth_limit.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace TcpBandwidthLimit {

class TcpBandwidthLimitFilterTest : public ::testing::Test {
public:
  Buffer::WatermarkBuffer& getReadBuffer() { return filter_->read_buffer_; }
  Buffer::WatermarkBuffer& getWriteBuffer() { return filter_->write_buffer_; }
  Event::TimerPtr& getReadTimer() { return filter_->read_timer_; }
  Event::TimerPtr& getWriteTimer() { return filter_->write_timer_; }

  void setup(const std::string& yaml) {
    envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);

    config_ = std::make_shared<FilterConfig>(proto_config, *stats_store_.rootScope(), runtime_,
                                             time_source_);
    filter_ = std::make_unique<TcpBandwidthLimitFilter>(config_);

    // Set a buffer limit so the WatermarkBuffer watermarks are active.
    ON_CALL(read_filter_callbacks_.connection_, bufferLimit()).WillByDefault(Return(1));
    ON_CALL(write_filter_callbacks_.connection_, bufferLimit()).WillByDefault(Return(1));
    filter_->initializeReadFilterCallbacks(read_filter_callbacks_);
    filter_->initializeWriteFilterCallbacks(write_filter_callbacks_);
  }

  NiceMock<Runtime::MockLoader> runtime_;
  Stats::IsolatedStoreImpl stats_store_;
  Event::SimulatedTimeSystem time_source_;
  NiceMock<Network::MockReadFilterCallbacks> read_filter_callbacks_;
  NiceMock<Network::MockWriteFilterCallbacks> write_filter_callbacks_;
  FilterConfigSharedPtr config_;
  std::unique_ptr<TcpBandwidthLimitFilter> filter_;
};

TEST_F(TcpBandwidthLimitFilterTest, ReadLimit) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data1(std::string(1024, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data1, false));

  Buffer::OwnedImpl data2(std::string(512, 'b'));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data2, false));

  // Verify upload passes through (no upload limit configured)
  Buffer::OwnedImpl upload_data(std::string(10000, 'c'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(upload_data, false));
}

TEST_F(TcpBandwidthLimitFilterTest, WriteLimit) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    write_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data1(std::string(1024, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(data1, false));

  Buffer::OwnedImpl data2(std::string(512, 'b'));
  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(data2, false));

  // Verify download passes through (no download limit configured)
  Buffer::OwnedImpl download_data(std::string(10000, 'c'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(download_data, false));
}

TEST_F(TcpBandwidthLimitFilterTest, RuntimeDisabled) {
  EXPECT_CALL(runtime_.snapshot_, featureEnabled("test_key", false)).WillRepeatedly(Return(false));

  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    runtime_enabled:
      default_value: false
      runtime_key: test_key
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data(std::string(200, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));
}

TEST_F(TcpBandwidthLimitFilterTest, BothLimitsConfigured) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    write_limit_kbps: 2
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl download_data(std::string(1024, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(download_data, false));

  Buffer::OwnedImpl upload_data(std::string(2048, 'b'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(upload_data, false));

  Buffer::OwnedImpl download_data2(std::string(512, 'c'));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(download_data2, false));

  Buffer::OwnedImpl upload_data2(std::string(512, 'd'));
  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(upload_data2, false));
}

TEST_F(TcpBandwidthLimitFilterTest, PartialConsumption) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl large_data(std::string(2048, 'a'));

  Buffer::OwnedImpl injected_data;
  EXPECT_CALL(read_filter_callbacks_, injectReadDataToFilterChain(_, false))
      .WillOnce(
          Invoke([&injected_data](Buffer::Instance& data, bool) { injected_data.move(data); }));

  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(large_data, false));

  EXPECT_EQ(1024, injected_data.length());
  EXPECT_EQ(std::string(1024, 'a'), injected_data.toString());

  EXPECT_EQ(0, large_data.length());
}

TEST_F(TcpBandwidthLimitFilterTest, PartialConsumptionWrite) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    write_limit_kbps: 1
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl large_data(std::string(2048, 'b'));

  Buffer::OwnedImpl written_data;
  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(write_filter_callbacks_, injectWriteDataToFilterChain(_, false))
      .WillOnce(Invoke([&written_data](Buffer::Instance& data, bool) { written_data.move(data); }));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(large_data, false));

  EXPECT_EQ(1024, written_data.length());
  EXPECT_EQ(std::string(1024, 'b'), written_data.toString());

  EXPECT_EQ(0, large_data.length());
}

TEST_F(TcpBandwidthLimitFilterTest, NoLimitPassThrough) {
  const std::string yaml = R"EOF(
    stat_prefix: test
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl large_download(std::string(10000, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(large_download, false));
  EXPECT_EQ(10000, large_download.length());

  Buffer::OwnedImpl large_upload(std::string(10000, 'b'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(large_upload, false));
  EXPECT_EQ(10000, large_upload.length());
}

TEST_F(TcpBandwidthLimitFilterTest, ZeroLimitBlocksAllRead) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 0
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data(std::string(1024, 'a'));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  EXPECT_EQ(0, data.length());
  EXPECT_CALL(read_filter_callbacks_, injectReadDataToFilterChain(_, _)).Times(0);
}

TEST_F(TcpBandwidthLimitFilterTest, ZeroLimitBlocksAllWrite) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    write_limit_kbps: 0
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data(std::string(1024, 'b'));
  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(data, false));

  EXPECT_EQ(0, data.length());
  EXPECT_CALL(write_filter_callbacks_, injectWriteDataToFilterChain(_, _)).Times(0);
}

TEST_F(TcpBandwidthLimitFilterTest, ConnectionClosed) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    write_limit_kbps: 1
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl empty_data1;
  Buffer::OwnedImpl empty_data2;
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(empty_data1, true));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(empty_data2, true));
}

TEST_F(TcpBandwidthLimitFilterTest, FillIntervalValidation) {
  // Test minimum fill interval (20ms)
  const std::string yaml1 = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 20000000
  )EOF";

  setup(yaml1);
  EXPECT_EQ(std::chrono::milliseconds(20), config_->fillInterval());

  // Test maximum fill interval (1s)
  const std::string yaml2 = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    fill_interval:
      seconds: 1
      nanos: 0
  )EOF";

  FilterConfigSharedPtr config2 = std::make_shared<FilterConfig>(
      TestUtility::parseYaml<
          envoy::extensions::filters::network::tcp_bandwidth_limit::v3::TcpBandwidthLimit>(yaml2),
      *stats_store_.rootScope(), runtime_, time_source_);
  EXPECT_EQ(std::chrono::milliseconds(1000), config2->fillInterval());
}

TEST_F(TcpBandwidthLimitFilterTest, DefaultFillInterval) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 100
  )EOF";

  setup(yaml);
  EXPECT_EQ(std::chrono::milliseconds(50), config_->fillInterval());
}

TEST_F(TcpBandwidthLimitFilterTest, EmptyDataWithLimits) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 100
    write_limit_kbps: 100
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl empty_data;
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(empty_data, false));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(empty_data, false));
}

TEST_F(TcpBandwidthLimitFilterTest, ProcessBufferedDataScenarios) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    write_limit_kbps: 1
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl download_data(std::string(2048, 'a'));
  Buffer::OwnedImpl injected_data;

  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  EXPECT_CALL(read_filter_callbacks_, injectReadDataToFilterChain(_, false))
      .WillOnce(
          Invoke([&injected_data](Buffer::Instance& data, bool) { injected_data.move(data); }));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(download_data, false));
  EXPECT_EQ(1024, injected_data.length());

  Buffer::OwnedImpl upload_data(std::string(2048, 'b'));
  Buffer::OwnedImpl written_data;

  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(write_filter_callbacks_, injectWriteDataToFilterChain(_, false))
      .WillOnce(Invoke([&written_data](Buffer::Instance& data, bool) { written_data.move(data); }));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(upload_data, false));
  EXPECT_EQ(1024, written_data.length());
}

TEST_F(TcpBandwidthLimitFilterTest, DestructorCleanup) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    write_limit_kbps: 1
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data(std::string(2048, 'a'));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, false));

  filter_.reset();
}

TEST_F(TcpBandwidthLimitFilterTest, ConfigAccessors) {
  const std::string yaml_with_limits = R"EOF(
    stat_prefix: test
    read_limit_kbps: 100
    write_limit_kbps: 50
    fill_interval:
      seconds: 0
      nanos: 100000000
  )EOF";

  setup(yaml_with_limits);

  EXPECT_TRUE(config_->hasReadLimit());
  EXPECT_TRUE(config_->hasWriteLimit());
  EXPECT_EQ(100, config_->readLimit());
  EXPECT_EQ(50, config_->writeLimit());
  EXPECT_EQ(std::chrono::milliseconds(100), config_->fillInterval());
  EXPECT_TRUE(config_->enabled());

  EXPECT_EQ(&runtime_, &config_->runtime());
  EXPECT_EQ(&time_source_, &config_->timeSource());

  const std::string yaml_no_limits = R"EOF(
    stat_prefix: test
  )EOF";

  setup(yaml_no_limits);

  EXPECT_FALSE(config_->hasReadLimit());
  EXPECT_FALSE(config_->hasWriteLimit());
  EXPECT_EQ(std::chrono::milliseconds(50), config_->fillInterval()); // Default 50ms
}

TEST_F(TcpBandwidthLimitFilterTest, OnNewConnectionTest) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 100
  )EOF";

  setup(yaml);

  auto new_filter = std::make_unique<TcpBandwidthLimitFilter>(config_);

  EXPECT_EQ(Network::FilterStatus::Continue, new_filter->onNewConnection());

  new_filter->initializeReadFilterCallbacks(read_filter_callbacks_);
  new_filter->initializeWriteFilterCallbacks(write_filter_callbacks_);

  Buffer::OwnedImpl data(std::string(100, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, new_filter->onData(data, false));
}

TEST_F(TcpBandwidthLimitFilterTest, ReadTimerBufferDraining) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Event::MockTimer* timer =
      new NiceMock<Event::MockTimer>(&read_filter_callbacks_.connection_.dispatcher_);
  ON_CALL(read_filter_callbacks_.connection_.dispatcher_, createTimer_(_))
      .WillByDefault(testing::Invoke([timer](Event::TimerCb) { return timer; }));

  Buffer::OwnedImpl data1(std::string(1024, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data1, false));

  Buffer::OwnedImpl data2(std::string(100, 'b'));
  ON_CALL(read_filter_callbacks_.connection_, readDisable(true))
      .WillByDefault(
          testing::Return(Network::Connection::ReadDisableStatus::TransitionedToReadDisabled));
  ON_CALL(*timer, enableTimer(std::chrono::milliseconds(50), _)).WillByDefault(testing::Return());

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data2, false));

  ON_CALL(read_filter_callbacks_, injectReadDataToFilterChain(_, false))
      .WillByDefault(testing::Return());
  ON_CALL(read_filter_callbacks_.connection_, readDisable(false))
      .WillByDefault(testing::Return(Network::Connection::ReadDisableStatus::NoTransition));

  filter_->onReadTokenTimer();
  filter_->onReadTokenTimer();
  filter_->onReadTokenTimer();
}

TEST_F(TcpBandwidthLimitFilterTest, WriteTimerBufferDraining) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    write_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Event::MockTimer* timer =
      new NiceMock<Event::MockTimer>(&read_filter_callbacks_.connection_.dispatcher_);
  ON_CALL(read_filter_callbacks_.connection_.dispatcher_, createTimer_(_))
      .WillByDefault(testing::Invoke([timer](Event::TimerCb) { return timer; }));

  Buffer::OwnedImpl data1(std::string(1024, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(data1, false));

  Buffer::OwnedImpl data2(std::string(100, 'b'));
  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  ON_CALL(*timer, enableTimer(std::chrono::milliseconds(50), _)).WillByDefault(testing::Return());

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(data2, false));

  ON_CALL(write_filter_callbacks_, injectWriteDataToFilterChain(_, _))
      .WillByDefault(testing::Return());
  ON_CALL(write_filter_callbacks_, onBelowWriteBufferLowWatermark())
      .WillByDefault(testing::Return());

  filter_->onWriteTokenTimer();
  filter_->onWriteTokenTimer();
  filter_->onWriteTokenTimer();
}

TEST_F(TcpBandwidthLimitFilterTest, SimultaneousTimers) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    write_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Event::MockTimer* download_timer =
      new NiceMock<Event::MockTimer>(&read_filter_callbacks_.connection_.dispatcher_);
  Event::MockTimer* upload_timer =
      new NiceMock<Event::MockTimer>(&read_filter_callbacks_.connection_.dispatcher_);
  int timer_count = 0;
  ON_CALL(read_filter_callbacks_.connection_.dispatcher_, createTimer_(_))
      .WillByDefault(testing::Invoke([&timer_count, download_timer, upload_timer](Event::TimerCb) {
        return (timer_count++ == 0) ? download_timer : upload_timer;
      }));

  Buffer::OwnedImpl data1(std::string(1024, 'x'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data1, false));
  Buffer::OwnedImpl data2(std::string(1024, 'y'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(data2, false));

  Buffer::OwnedImpl download_data(std::string(512, 'a'));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  ON_CALL(*download_timer, enableTimer(std::chrono::milliseconds(50), _))
      .WillByDefault(testing::Return());
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(download_data, false));

  Buffer::OwnedImpl upload_data(std::string(512, 'b'));
  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  ON_CALL(*upload_timer, enableTimer(std::chrono::milliseconds(50), _))
      .WillByDefault(testing::Return());
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(upload_data, false));

  ON_CALL(read_filter_callbacks_, injectReadDataToFilterChain(_, false))
      .WillByDefault(testing::Return());
  ON_CALL(read_filter_callbacks_.connection_, readDisable(false))
      .WillByDefault(testing::Return(Network::Connection::ReadDisableStatus::NoTransition));
  ON_CALL(write_filter_callbacks_, injectWriteDataToFilterChain(_, _))
      .WillByDefault(testing::Return());

  filter_->onReadTokenTimer();

  filter_->onWriteTokenTimer();

  // Verify both can be called again without issues
  filter_->onReadTokenTimer();
  filter_->onWriteTokenTimer();
}

// Test edge case: throttling with small amounts of data
TEST_F(TcpBandwidthLimitFilterTest, TimerWithEmptyBuffer) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
  )EOF";

  setup(yaml);

  Event::MockTimer* timer =
      new NiceMock<Event::MockTimer>(&read_filter_callbacks_.connection_.dispatcher_);
  ON_CALL(read_filter_callbacks_.connection_.dispatcher_, createTimer_(_))
      .WillByDefault(testing::Invoke([timer](Event::TimerCb) { return timer; }));

  Buffer::OwnedImpl data1(std::string(1024, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data1, false));

  Buffer::OwnedImpl data2(std::string(100, 'b'));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  ON_CALL(*timer, enableTimer(std::chrono::milliseconds(50), _)).WillByDefault(testing::Return());
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data2, false));

  ON_CALL(read_filter_callbacks_, injectReadDataToFilterChain(_, false))
      .WillByDefault(testing::Return());
  ON_CALL(read_filter_callbacks_.connection_, readDisable(false))
      .WillByDefault(testing::Return(Network::Connection::ReadDisableStatus::NoTransition));

  // First timer fire: processes the 100 bytes of buffered data
  // This should inject the buffered data and may re-enable the timer
  filter_->onReadTokenTimer();

  // Subsequent fires: handle the empty buffer case
  // When buffer is empty, timer should be reset and read re-enabled
  filter_->onReadTokenTimer();

  // One more call to ensure empty buffer handling is robust
  filter_->onReadTokenTimer();
}

// Test stats increment for throttling
TEST_F(TcpBandwidthLimitFilterTest, StatsIncrement) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    write_limit_kbps: 1
  )EOF";

  setup(yaml);

  EXPECT_EQ(0, stats_store_.counterFromString("test.tcp_bandwidth_limit.read_enabled").value());
  EXPECT_EQ(0, stats_store_.counterFromString("test.tcp_bandwidth_limit.write_enabled").value());
  EXPECT_EQ(0, stats_store_.counterFromString("test.tcp_bandwidth_limit.read_throttled").value());
  EXPECT_EQ(0, stats_store_.counterFromString("test.tcp_bandwidth_limit.write_throttled").value());

  Buffer::OwnedImpl data1(std::string(512, 'a'));
  filter_->onData(data1, false);
  EXPECT_EQ(1, stats_store_.counterFromString("test.tcp_bandwidth_limit.read_enabled").value());

  Buffer::OwnedImpl data2(std::string(512, 'b'));
  filter_->onWrite(data2, false);
  EXPECT_EQ(1, stats_store_.counterFromString("test.tcp_bandwidth_limit.write_enabled").value());

  Buffer::OwnedImpl large_data(std::string(2048, 'c'));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  filter_->onData(large_data, false);
  EXPECT_EQ(2, stats_store_.counterFromString("test.tcp_bandwidth_limit.read_enabled").value());
  EXPECT_EQ(1, stats_store_.counterFromString("test.tcp_bandwidth_limit.read_throttled").value());
  EXPECT_EQ(1536, stats_store_
                      .gaugeFromString("test.tcp_bandwidth_limit.read_bytes_buffered",
                                       Stats::Gauge::ImportMode::Accumulate)
                      .value());

  Buffer::OwnedImpl large_upload(std::string(2048, 'd'));
  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  filter_->onWrite(large_upload, false);
  EXPECT_EQ(2, stats_store_.counterFromString("test.tcp_bandwidth_limit.write_enabled").value());
  EXPECT_EQ(1, stats_store_.counterFromString("test.tcp_bandwidth_limit.write_throttled").value());
  EXPECT_EQ(1536, stats_store_
                      .gaugeFromString("test.tcp_bandwidth_limit.write_bytes_buffered",
                                       Stats::Gauge::ImportMode::Accumulate)
                      .value());
}

TEST_F(TcpBandwidthLimitFilterTest, TimerReEnableReadAlreadyEnabled) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl initial_data(std::string(1024, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(initial_data, false));

  Buffer::OwnedImpl more_data(std::string(512, 'b'));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(more_data, false));

  ON_CALL(read_filter_callbacks_.connection_, readDisable(false))
      .WillByDefault(Return(Network::Connection::ReadDisableStatus::NoTransition));

  Buffer::OwnedImpl final_data(std::string(256, 'c'));
  filter_->onData(final_data, false);
}

// Test timer callback when buffer becomes empty
TEST_F(TcpBandwidthLimitFilterTest, ReadTimerEmptyBufferPath) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 10
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data(std::string(100, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data, false));

  filter_->onReadTokenTimer();
}

// Test upload timer callback when buffer becomes empty
TEST_F(TcpBandwidthLimitFilterTest, WriteTimerEmptyBufferPath) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    write_limit_kbps: 10
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data(std::string(100, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(data, false));

  filter_->onWriteTokenTimer();
}

TEST_F(TcpBandwidthLimitFilterTest, PartialTokenConsumptionRead) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data1(std::string(900, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data1, false));

  Buffer::OwnedImpl data2(std::string(500, 'b'));

  Buffer::OwnedImpl injected_data;
  EXPECT_CALL(read_filter_callbacks_, injectReadDataToFilterChain(_, false))
      .WillOnce(
          Invoke([&injected_data](Buffer::Instance& data, bool) { injected_data.move(data); }));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data2, false));
  EXPECT_GT(injected_data.length(), 0);
  EXPECT_LT(injected_data.length(), 500);
}

TEST_F(TcpBandwidthLimitFilterTest, PartialTokenConsumptionWrite) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    write_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data1(std::string(900, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(data1, false));

  Buffer::OwnedImpl data2(std::string(500, 'b'));

  Buffer::OwnedImpl written_data;
  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(write_filter_callbacks_, injectWriteDataToFilterChain(_, false))
      .WillOnce(Invoke([&written_data](Buffer::Instance& data, bool) { written_data.move(data); }));

  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(data2, false));
  EXPECT_GT(written_data.length(), 0);
  EXPECT_LT(written_data.length(), 500);
}

// Edge case: Connection closing while data is buffered
TEST_F(TcpBandwidthLimitFilterTest, ConnectionClosingWithBufferedData) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data1(std::string(1024, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data1, false));

  Buffer::OwnedImpl data2(std::string(512, 'b'));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data2, false));
}

TEST_F(TcpBandwidthLimitFilterTest, ProcessBufferedReadDataEmptyBuffer) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
  )EOF";

  setup(yaml);

  // Directly call with empty buffer should return early
  filter_->onReadTokenTimer();
}

TEST_F(TcpBandwidthLimitFilterTest, ProcessBufferedReadDataNoTokenBucket) {
  const std::string yaml = R"EOF(
    stat_prefix: test
  )EOF";

  setup(yaml);

  // Directly call with empty buffer should return early
  filter_->onReadTokenTimer();
}

TEST_F(TcpBandwidthLimitFilterTest, ProcessBufferedWriteDataEmptyBuffer) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    write_limit_kbps: 1
  )EOF";

  setup(yaml);

  // Directly call with empty buffer should return early
  filter_->onWriteTokenTimer();
}

TEST_F(TcpBandwidthLimitFilterTest, ProcessBufferedWriteDataNoTokenBucket) {
  const std::string yaml = R"EOF(
    stat_prefix: test
  )EOF";

  setup(yaml);

  // Directly call with empty buffer should return early
  filter_->onWriteTokenTimer();
}

TEST_F(TcpBandwidthLimitFilterTest, ReadTimerDrainsBuffer) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data(std::string(2048, 'x'));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  EXPECT_CALL(read_filter_callbacks_, injectReadDataToFilterChain(_, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data, true));

  // Advance time so the token bucket refills
  time_source_.advanceTimeWait(std::chrono::milliseconds(1100));

  EXPECT_CALL(read_filter_callbacks_, injectReadDataToFilterChain(_, true));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(false));
  filter_->onReadTokenTimer();

  EXPECT_EQ(0, getReadBuffer().length());
  EXPECT_EQ(nullptr, getReadTimer());
}

TEST_F(TcpBandwidthLimitFilterTest, WriteTimerDrainsBuffer) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    write_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  Buffer::OwnedImpl data(std::string(2048, 'x'));
  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(write_filter_callbacks_, injectWriteDataToFilterChain(_, false));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(data, true));

  // Advance time so the token bucket refills
  time_source_.advanceTimeWait(std::chrono::milliseconds(1100));

  EXPECT_CALL(write_filter_callbacks_, injectWriteDataToFilterChain(_, true));
  EXPECT_CALL(write_filter_callbacks_, onBelowWriteBufferLowWatermark());
  filter_->onWriteTokenTimer();

  EXPECT_EQ(0, getWriteBuffer().length());
  EXPECT_EQ(nullptr, getWriteTimer());
}

TEST_F(TcpBandwidthLimitFilterTest, WriteTimerResetPath) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    write_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  // First trigger buffering
  Buffer::OwnedImpl data(std::string(2048, 'x'));
  auto* timer = new Event::MockTimer(&read_filter_callbacks_.connection_.dispatcher_);
  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(50), nullptr));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(data, false));

  // Clear the buffer completely to test the empty buffer path
  getWriteBuffer().drain(getWriteBuffer().length());
  filter_->onWriteTokenTimer();

  // Verify timer was reset
  EXPECT_EQ(nullptr, getWriteTimer());
}

TEST_F(TcpBandwidthLimitFilterTest, ProcessBufferedReadWithTokens) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  getReadBuffer().add("test data");
  getReadTimer().reset(new Event::MockTimer());

  time_source_.advanceTimeWait(std::chrono::milliseconds(100));
  EXPECT_CALL(read_filter_callbacks_,
              injectReadDataToFilterChain(BufferStringEqual("test data"), false));

  filter_->onReadTokenTimer();

  // Verify buffer is now empty and timer is reset
  EXPECT_EQ(0, getReadBuffer().length());
  EXPECT_EQ(nullptr, getReadTimer());
}

TEST_F(TcpBandwidthLimitFilterTest, ProcessBufferedWriteWithTokens) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    write_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  getWriteBuffer().add("test data");
  getWriteTimer().reset(new Event::MockTimer());

  time_source_.advanceTimeWait(std::chrono::milliseconds(100));

  EXPECT_CALL(write_filter_callbacks_,
              injectWriteDataToFilterChain(BufferStringEqual("test data"), false));
  EXPECT_CALL(write_filter_callbacks_, onBelowWriteBufferLowWatermark());

  filter_->onWriteTokenTimer();

  // Verify buffer is now empty and timer is reset
  EXPECT_EQ(0, getWriteBuffer().length());
  EXPECT_EQ(nullptr, getWriteTimer());
}

// Test that new data is buffered when there's already pending data, to preserve byte ordering.
TEST_F(TcpBandwidthLimitFilterTest, ReadOrderingWithPendingBuffer) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    read_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  // First call: consume all tokens
  Buffer::OwnedImpl data1(std::string(1024, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onData(data1, false));

  // Second call: partial consumption, buffer remainder
  Buffer::OwnedImpl data2(std::string(512, 'b'));
  EXPECT_CALL(read_filter_callbacks_.connection_, readDisable(true));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data2, false));
  EXPECT_EQ(512, getReadBuffer().length());

  // Third call: buffer has pending data. New data must be appended without consuming tokens.
  EXPECT_CALL(read_filter_callbacks_, injectReadDataToFilterChain(_, _)).Times(0);
  Buffer::OwnedImpl data3(std::string(256, 'c'));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onData(data3, false));
  EXPECT_EQ(512 + 256, getReadBuffer().length());
}

TEST_F(TcpBandwidthLimitFilterTest, WriteOrderingWithPendingBuffer) {
  const std::string yaml = R"EOF(
    stat_prefix: test
    write_limit_kbps: 1
    fill_interval:
      seconds: 0
      nanos: 50000000
  )EOF";

  setup(yaml);

  // First call: consume all tokens
  Buffer::OwnedImpl data1(std::string(1024, 'a'));
  EXPECT_EQ(Network::FilterStatus::Continue, filter_->onWrite(data1, false));

  // Second call: partial consumption, buffer remainder
  Buffer::OwnedImpl data2(std::string(512, 'b'));
  EXPECT_CALL(write_filter_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(data2, false));
  EXPECT_EQ(512, getWriteBuffer().length());

  // Third call: buffer has pending data. New data must be appended without consuming tokens.
  EXPECT_CALL(write_filter_callbacks_, injectWriteDataToFilterChain(_, _)).Times(0);
  Buffer::OwnedImpl data3(std::string(256, 'c'));
  EXPECT_EQ(Network::FilterStatus::StopIteration, filter_->onWrite(data3, false));
  EXPECT_EQ(512 + 256, getWriteBuffer().length());
}

} // namespace TcpBandwidthLimit
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
