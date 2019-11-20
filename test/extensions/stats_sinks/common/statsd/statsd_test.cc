#include <chrono>
#include <memory>

#include "common/network/utility.h"
#include "common/upstream/upstream_impl.h"

#include "extensions/stat_sinks/common/statsd/statsd.h"

#include "test/common/upstream/utility.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/local_info/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Common {
namespace Statsd {
namespace {

class TcpStatsdSinkTest : public testing::Test {
public:
  TcpStatsdSinkTest() {
    sink_ = std::make_unique<TcpStatsdSink>(
        local_info_, "fake_cluster", tls_, cluster_manager_,
        cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_);
  }

  void expectCreateConnection() {
    connection_ = new NiceMock<Network::MockClientConnection>();
    Upstream::MockHost::MockCreateConnectionData conn_info;
    conn_info.connection_ = connection_;
    conn_info.host_description_ = Upstream::makeTestHost(
        std::make_unique<NiceMock<Upstream::MockClusterInfo>>(), "tcp://127.0.0.1:80");

    EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster", _))
        .WillOnce(Return(conn_info));
    EXPECT_CALL(*connection_, setConnectionStats(_));
    EXPECT_CALL(*connection_, connect());
  }

  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  std::unique_ptr<TcpStatsdSink> sink_;
  NiceMock<LocalInfo::MockLocalInfo> local_info_;
  Network::MockClientConnection* connection_{};
  NiceMock<Stats::MockMetricSnapshot> snapshot_;
};

TEST_F(TcpStatsdSinkTest, EmptyFlush) {
  InSequence s;
  expectCreateConnection();
  EXPECT_CALL(*connection_, write(BufferStringEqual(""), _));
  sink_->flush(snapshot_);
}

TEST_F(TcpStatsdSinkTest, BasicFlow) {
  InSequence s;
  auto counter = std::make_shared<NiceMock<Stats::MockCounter>>();
  counter->name_ = "test_counter";
  counter->latch_ = 1;
  counter->used_ = true;
  snapshot_.counters_.push_back({1, *counter});

  auto gauge = std::make_shared<NiceMock<Stats::MockGauge>>();
  gauge->name_ = "test_gauge";
  gauge->value_ = 2;
  gauge->used_ = true;
  snapshot_.gauges_.push_back(*gauge);

  expectCreateConnection();
  EXPECT_CALL(*connection_,
              write(BufferStringEqual("envoy.test_counter:1|c\nenvoy.test_gauge:2|g\n"), _));
  sink_->flush(snapshot_);

  connection_->runHighWatermarkCallbacks();
  connection_->runLowWatermarkCallbacks();

  // Test a disconnect. We should connect again.
  connection_->raiseEvent(Network::ConnectionEvent::RemoteClose);

  expectCreateConnection();

  NiceMock<Stats::MockHistogram> timer;
  timer.name_ = "test_timer";
  EXPECT_CALL(*connection_, write(BufferStringEqual("envoy.test_timer:5|ms\n"), _));
  sink_->onHistogramComplete(timer, 5);

  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));
  tls_.shutdownThread();
}

TEST_F(TcpStatsdSinkTest, SiSuffix) {
  InSequence s;
  expectCreateConnection();

  NiceMock<Stats::MockHistogram> items;
  items.name_ = "items";
  items.unit_ = Stats::Histogram::Unit::Unspecified;

  EXPECT_CALL(*connection_, write(BufferStringEqual("envoy.items:1|ms\n"), _));
  sink_->onHistogramComplete(items, 1);

  NiceMock<Stats::MockHistogram> information;
  information.name_ = "information";
  information.unit_ = Stats::Histogram::Unit::Bytes;

  EXPECT_CALL(*connection_, write(BufferStringEqual("envoy.information:2|ms\n"), _));
  sink_->onHistogramComplete(information, 2);

  NiceMock<Stats::MockHistogram> duration_micro;
  duration_micro.name_ = "duration";
  duration_micro.unit_ = Stats::Histogram::Unit::Microseconds;

  EXPECT_CALL(*connection_, write(BufferStringEqual("envoy.duration:3|ms\n"), _));
  sink_->onHistogramComplete(duration_micro, 3);

  NiceMock<Stats::MockHistogram> duration_milli;
  duration_milli.name_ = "duration";
  duration_milli.unit_ = Stats::Histogram::Unit::Milliseconds;

  EXPECT_CALL(*connection_, write(BufferStringEqual("envoy.duration:4|ms\n"), _));
  sink_->onHistogramComplete(duration_milli, 4);

  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));
  tls_.shutdownThread();
}

// Verify that when there is no statsd host we correctly empty all output buffers so we don't
// infinitely buffer.
TEST_F(TcpStatsdSinkTest, NoHost) {
  InSequence s;
  auto counter = std::make_shared<NiceMock<Stats::MockCounter>>();
  counter->name_ = "test_counter";
  counter->latch_ = 1;
  counter->used_ = true;
  snapshot_.counters_.push_back({1, *counter});

  Upstream::MockHost::MockCreateConnectionData conn_info;
  EXPECT_CALL(cluster_manager_, tcpConnForCluster_("fake_cluster", _))
      .WillOnce(Return(conn_info))
      .WillOnce(Return(conn_info));
  sink_->flush(snapshot_);

  // Flush again to make sure we correctly drain the buffer and the output buffer is empty.
  sink_->flush(snapshot_);
}

TEST_F(TcpStatsdSinkTest, WithCustomPrefix) {
  sink_ = std::make_unique<TcpStatsdSink>(
      local_info_, "fake_cluster", tls_, cluster_manager_,
      cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_, "test_prefix");

  auto counter = std::make_shared<NiceMock<Stats::MockCounter>>();
  counter->name_ = "test_counter";
  counter->latch_ = 1;
  counter->used_ = true;
  snapshot_.counters_.push_back({1, *counter});

  expectCreateConnection();
  EXPECT_CALL(*connection_, write(BufferStringEqual("test_prefix.test_counter:1|c\n"), _));
  sink_->flush(snapshot_);
}

TEST_F(TcpStatsdSinkTest, BufferReallocate) {
  InSequence s;

  auto counter = std::make_shared<NiceMock<Stats::MockCounter>>();
  counter->name_ = "test_counter";
  counter->latch_ = 1;
  counter->used_ = true;

  snapshot_.counters_.resize(2000, {1, *counter});

  expectCreateConnection();
  EXPECT_CALL(*connection_, write(_, _))
      .WillOnce(Invoke([](Buffer::Instance& buffer, bool) -> void {
        std::string compare;
        for (int i = 0; i < 2000; i++) {
          compare += "envoy.test_counter:1|c\n";
        }
        EXPECT_EQ(compare, buffer.toString());
        buffer.drain(buffer.length());
      }));
  sink_->flush(snapshot_);
}

TEST_F(TcpStatsdSinkTest, Overflow) {
  InSequence s;

  auto counter = std::make_shared<NiceMock<Stats::MockCounter>>();
  counter->name_ = "test_counter";
  counter->latch_ = 1;
  counter->used_ = true;
  snapshot_.counters_.push_back({1, *counter});

  // Synthetically set buffer above high watermark. Make sure we don't write anything.
  cluster_manager_.thread_local_cluster_.cluster_.info_->stats().upstream_cx_tx_bytes_buffered_.set(
      1024 * 1024 * 17);
  sink_->flush(snapshot_);

  // Lower and make sure we write.
  cluster_manager_.thread_local_cluster_.cluster_.info_->stats().upstream_cx_tx_bytes_buffered_.set(
      1024 * 1024 * 15);
  expectCreateConnection();
  EXPECT_CALL(*connection_, write(BufferStringEqual("envoy.test_counter:1|c\n"), _));
  sink_->flush(snapshot_);

  // Raise and make sure we don't write and kill connection.
  cluster_manager_.thread_local_cluster_.cluster_.info_->stats().upstream_cx_tx_bytes_buffered_.set(
      1024 * 1024 * 17);
  EXPECT_CALL(*connection_, close(Network::ConnectionCloseType::NoFlush));
  sink_->flush(snapshot_);

  EXPECT_EQ(2UL, cluster_manager_.thread_local_cluster_.cluster_.info_->stats_store_
                     .counter("statsd.cx_overflow")
                     .value());
  tls_.shutdownThread();
}

} // namespace
} // namespace Statsd
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
