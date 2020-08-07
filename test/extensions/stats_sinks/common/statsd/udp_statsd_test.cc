#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "common/api/os_sys_calls_impl.h"
#include "common/network/address_impl.h"
#include "common/network/socket_impl.h"
#include "common/network/utility.h"

#include "extensions/stat_sinks/common/statsd/statsd.h"

#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "spdlog/spdlog.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace StatSinks {
namespace Common {
namespace Statsd {
namespace {

class MockWriter : public UdpStatsdSink::Writer {
public:
  MOCK_METHOD(void, write, (const std::string& message));
  MOCK_METHOD(void, writeBuffer, (Buffer::Instance & buffer));

  void delegateBufferFake() {
    ON_CALL(*this, writeBuffer).WillByDefault([this](Buffer::Instance& buffer) {
      this->buffer_writes.push_back(buffer.toString());
    });
  }

  std::vector<std::string> buffer_writes;
};

// Regression test for https://github.com/envoyproxy/envoy/issues/8911
TEST(UdpOverUdsStatsdSinkTest, InitWithPipeAddress) {
  auto uds_address = std::make_shared<Network::Address::PipeInstance>(
      TestEnvironment::unixDomainSocketPath("udstest.1.sock"));
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  UdpStatsdSink sink(tls_, uds_address, false);

  NiceMock<Stats::MockCounter> counter;
  counter.name_ = "test_counter";
  counter.used_ = true;
  counter.latch_ = 1;
  snapshot.counters_.push_back({1, counter});

  // Flush before the server is running. This will fail.
  sink.flush(snapshot);

  // Start the server.
  Network::SocketImpl sock(Network::Socket::Type::Datagram, uds_address);
  RELEASE_ASSERT(sock.setBlockingForTest(false).rc_ != -1, "");
  sock.bind(uds_address);

  // Do the flush which should have somewhere to write now.
  sink.flush(snapshot);
  Buffer::OwnedImpl receive_buffer;
  receive_buffer.read(sock.ioHandle(), 32);
  EXPECT_EQ("envoy.test_counter:1|c", receive_buffer.toString());
}

class UdpStatsdSinkTest : public testing::TestWithParam<Network::Address::IpVersion> {};
INSTANTIATE_TEST_SUITE_P(IpVersions, UdpStatsdSinkTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(UdpStatsdSinkTest, InitWithIpAddress) {
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  Network::Test::UdpSyncPeer server(GetParam());
  UdpStatsdSink sink(tls_, server.localAddress(), false);

  NiceMock<Stats::MockCounter> counter;
  counter.name_ = "test_counter";
  counter.used_ = true;
  counter.latch_ = 1;
  snapshot.counters_.push_back({1, counter});

  NiceMock<Stats::MockGauge> gauge;
  gauge.name_ = "test_gauge";
  gauge.value_ = 1;
  gauge.used_ = true;
  snapshot.gauges_.push_back(gauge);

  sink.flush(snapshot);
  Network::UdpRecvData data;
  server.recv(data);
  EXPECT_EQ("envoy.test_counter:1|c", data.buffer_->toString());
  Network::UdpRecvData data2;
  server.recv(data2);
  EXPECT_EQ("envoy.test_gauge:1|g", data2.buffer_->toString());

  NiceMock<Stats::MockHistogram> timer;
  timer.name_ = "test_timer";
  sink.onHistogramComplete(timer, 5);
  Network::UdpRecvData data3;
  server.recv(data3);
  EXPECT_EQ("envoy.test_timer:5|ms", data3.buffer_->toString());

  tls_.shutdownThread();
}

class UdpStatsdSinkWithTagsTest : public testing::TestWithParam<Network::Address::IpVersion> {};
INSTANTIATE_TEST_SUITE_P(IpVersions, UdpStatsdSinkWithTagsTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(UdpStatsdSinkWithTagsTest, InitWithIpAddress) {
  NiceMock<ThreadLocal::MockInstance> tls_;
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  Network::Test::UdpSyncPeer server(GetParam());
  UdpStatsdSink sink(tls_, server.localAddress(), true);

  std::vector<Stats::Tag> tags = {Stats::Tag{"node", "test"}};
  NiceMock<Stats::MockCounter> counter;
  counter.name_ = "test_counter";
  counter.used_ = true;
  counter.latch_ = 1;
  counter.setTags(tags);
  snapshot.counters_.push_back({1, counter});

  NiceMock<Stats::MockGauge> gauge;
  gauge.name_ = "test_gauge";
  gauge.value_ = 1;
  gauge.used_ = true;
  gauge.setTags(tags);
  snapshot.gauges_.push_back(gauge);

  sink.flush(snapshot);
  Network::UdpRecvData data;
  server.recv(data);
  EXPECT_EQ("envoy.test_counter:1|c|#node:test", data.buffer_->toString());
  Network::UdpRecvData data2;
  server.recv(data2);
  EXPECT_EQ("envoy.test_gauge:1|g|#node:test", data2.buffer_->toString());

  NiceMock<Stats::MockHistogram> timer;
  timer.name_ = "test_timer";
  timer.setTags(tags);
  sink.onHistogramComplete(timer, 5);
  Network::UdpRecvData data3;
  server.recv(data3);
  EXPECT_EQ("envoy.test_timer:5|ms|#node:test", data3.buffer_->toString());

  tls_.shutdownThread();
}

TEST(UdpStatsdSinkTest, CheckActualStats) {
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  auto writer_ptr = std::make_shared<NiceMock<MockWriter>>();
  writer_ptr->delegateBufferFake();
  NiceMock<ThreadLocal::MockInstance> tls_;
  UdpStatsdSink sink(tls_, writer_ptr, false, getDefaultPrefix(), 1024);

  NiceMock<Stats::MockCounter> counter;
  counter.name_ = "test_counter";
  counter.used_ = true;
  counter.latch_ = 1;
  snapshot.counters_.push_back({1, counter});

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr), writeBuffer(_))
      .Times(1);
  sink.flush(snapshot);
  EXPECT_EQ(writer_ptr->buffer_writes.size(), 1);
  EXPECT_EQ(writer_ptr->buffer_writes.at(0), "envoy.test_counter:1|c");
  counter.used_ = false;

  NiceMock<Stats::MockGauge> gauge;
  gauge.name_ = "test_gauge";
  gauge.value_ = 1;
  gauge.used_ = true;
  snapshot.gauges_.push_back(gauge);

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr), writeBuffer(_));
  sink.flush(snapshot);
  EXPECT_EQ(writer_ptr->buffer_writes.size(), 2);
  EXPECT_EQ(writer_ptr->buffer_writes.at(1), "envoy.test_gauge:1|g");

  NiceMock<Stats::MockHistogram> timer;
  timer.name_ = "test_timer";
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.test_timer:5|ms"));
  sink.onHistogramComplete(timer, 5);

  tls_.shutdownThread();
}

TEST(UdpStatsdSinkTest, CheckMetricLargerThanBuffer) {
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  auto writer_ptr = std::make_shared<NiceMock<MockWriter>>();
  writer_ptr->delegateBufferFake();
  NiceMock<ThreadLocal::MockInstance> tls_;
  uint64_t buffer_size = 4;
  UdpStatsdSink sink(tls_, writer_ptr, false, getDefaultPrefix(), buffer_size);

  NiceMock<Stats::MockCounter> counter;
  counter.name_ = "test_counter";
  counter.used_ = true;
  counter.latch_ = 1;
  snapshot.counters_.push_back({1, counter});

  // Expect the metric to skip the buffer
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.test_counter:1|c"));
  sink.flush(snapshot);
  counter.used_ = false;

  NiceMock<Stats::MockGauge> gauge;
  gauge.name_ = "test_gauge";
  gauge.value_ = 1;
  gauge.used_ = true;
  snapshot.gauges_.push_back(gauge);

  // Expect the metric to skip the buffer
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.test_gauge:1|g"));
  sink.flush(snapshot);

  tls_.shutdownThread();
}

TEST(UdpStatsdSinkTest, CheckBufferedWritesWithinBufferSize) {
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  auto writer_ptr = std::make_shared<NiceMock<MockWriter>>();
  writer_ptr->delegateBufferFake();
  NiceMock<ThreadLocal::MockInstance> tls_;
  uint64_t buffer_size = 1024;
  UdpStatsdSink sink(tls_, writer_ptr, false, getDefaultPrefix(), buffer_size);

  NiceMock<Stats::MockCounter> counter;
  counter.name_ = "test_counter";
  counter.used_ = true;
  counter.latch_ = 1;
  snapshot.counters_.push_back({1, counter});

  NiceMock<Stats::MockGauge> gauge;
  gauge.name_ = "test_gauge";
  gauge.value_ = 1;
  gauge.used_ = true;
  snapshot.gauges_.push_back(gauge);

  // Expect both metrics to be present in single write
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr), writeBuffer(_))
      .Times(1);
  sink.flush(snapshot);
  EXPECT_EQ(writer_ptr->buffer_writes.size(), 1);
  EXPECT_EQ(writer_ptr->buffer_writes.at(0), "envoy.test_counter:1|c\nenvoy.test_gauge:1|g");

  tls_.shutdownThread();
}

TEST(UdpStatsdSinkTest, CheckBufferedWritesExceedingBufferSize) {
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  auto writer_ptr = std::make_shared<NiceMock<MockWriter>>();
  writer_ptr->delegateBufferFake();
  NiceMock<ThreadLocal::MockInstance> tls_;
  uint64_t buffer_size = 64;
  UdpStatsdSink sink(tls_, writer_ptr, false, getDefaultPrefix(), buffer_size);

  NiceMock<Stats::MockCounter> counter_1;
  counter_1.name_ = "test_counter_1";
  counter_1.used_ = true;
  counter_1.latch_ = 1;
  snapshot.counters_.push_back({1, counter_1});

  NiceMock<Stats::MockCounter> counter_2;
  counter_2.name_ = "test_counter_2";
  counter_2.used_ = true;
  counter_2.latch_ = 1;
  snapshot.counters_.push_back({1, counter_2});

  NiceMock<Stats::MockGauge> gauge;
  gauge.name_ = "test_gauge";
  gauge.value_ = 1;
  gauge.used_ = true;
  snapshot.gauges_.push_back(gauge);

  // Expect both metrics to be present in single write
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr), writeBuffer(_))
      .Times(2);
  sink.flush(snapshot);
  EXPECT_EQ(writer_ptr->buffer_writes.size(), 2);
  EXPECT_EQ(writer_ptr->buffer_writes.at(0), "envoy.test_counter_1:1|c\nenvoy.test_counter_2:1|c");
  EXPECT_EQ(writer_ptr->buffer_writes.at(1), "envoy.test_gauge:1|g");

  tls_.shutdownThread();
}

TEST(UdpStatsdSinkTest, CheckActualStatsWithCustomPrefix) {
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  auto writer_ptr = std::make_shared<NiceMock<MockWriter>>();
  writer_ptr->delegateBufferFake();
  NiceMock<ThreadLocal::MockInstance> tls_;
  UdpStatsdSink sink(tls_, writer_ptr, false, "test_prefix", 1024);

  NiceMock<Stats::MockCounter> counter;
  counter.name_ = "test_counter";
  counter.used_ = true;
  counter.latch_ = 1;
  snapshot.counters_.push_back({1, counter});

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr), writeBuffer(_));
  sink.flush(snapshot);
  EXPECT_EQ(writer_ptr->buffer_writes.size(), 1);
  EXPECT_EQ(writer_ptr->buffer_writes.at(0), "test_prefix.test_counter:1|c");
  counter.used_ = false;

  tls_.shutdownThread();
}

TEST(UdpStatsdSinkTest, SiSuffix) {
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  auto writer_ptr = std::make_shared<NiceMock<MockWriter>>();
  NiceMock<ThreadLocal::MockInstance> tls_;
  UdpStatsdSink sink(tls_, writer_ptr, false);

  NiceMock<Stats::MockHistogram> items;
  items.name_ = "items";
  items.unit_ = Stats::Histogram::Unit::Unspecified;

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.items:1|ms"));
  sink.onHistogramComplete(items, 1);

  NiceMock<Stats::MockHistogram> information;
  information.name_ = "information";
  information.unit_ = Stats::Histogram::Unit::Bytes;

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.information:2|ms"));
  sink.onHistogramComplete(information, 2);

  NiceMock<Stats::MockHistogram> duration_micro;
  duration_micro.name_ = "duration";
  duration_micro.unit_ = Stats::Histogram::Unit::Microseconds;

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.duration:3|ms"));
  sink.onHistogramComplete(duration_micro, 3);

  NiceMock<Stats::MockHistogram> duration_milli;
  duration_milli.name_ = "duration";
  duration_milli.unit_ = Stats::Histogram::Unit::Milliseconds;

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.duration:4|ms"));
  sink.onHistogramComplete(duration_milli, 4);

  tls_.shutdownThread();
}

TEST(UdpStatsdSinkWithTagsTest, CheckActualStats) {
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  auto writer_ptr = std::make_shared<NiceMock<MockWriter>>();
  writer_ptr->delegateBufferFake();
  NiceMock<ThreadLocal::MockInstance> tls_;
  UdpStatsdSink sink(tls_, writer_ptr, true, getDefaultPrefix(), 1024);

  std::vector<Stats::Tag> tags = {Stats::Tag{"key1", "value1"}, Stats::Tag{"key2", "value2"}};
  NiceMock<Stats::MockCounter> counter;
  counter.name_ = "test_counter";
  counter.used_ = true;
  counter.latch_ = 1;
  counter.setTags(tags);
  snapshot.counters_.push_back({1, counter});

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr), writeBuffer(_));
  sink.flush(snapshot);
  EXPECT_EQ(writer_ptr->buffer_writes.size(), 1);
  EXPECT_EQ(writer_ptr->buffer_writes.at(0), "envoy.test_counter:1|c|#key1:value1,key2:value2");
  counter.used_ = false;

  NiceMock<Stats::MockGauge> gauge;
  gauge.name_ = "test_gauge";
  gauge.value_ = 1;
  gauge.used_ = true;
  gauge.setTags(tags);
  snapshot.gauges_.push_back(gauge);

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr), writeBuffer(_));
  sink.flush(snapshot);
  EXPECT_EQ(writer_ptr->buffer_writes.size(), 2);
  EXPECT_EQ(writer_ptr->buffer_writes.at(1), "envoy.test_gauge:1|g|#key1:value1,key2:value2");

  NiceMock<Stats::MockHistogram> timer;
  timer.name_ = "test_timer";
  timer.setTags(tags);
  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.test_timer:5|ms|#key1:value1,key2:value2"));
  sink.onHistogramComplete(timer, 5);

  tls_.shutdownThread();
}

TEST(UdpStatsdSinkWithTagsTest, SiSuffix) {
  NiceMock<Stats::MockMetricSnapshot> snapshot;
  auto writer_ptr = std::make_shared<NiceMock<MockWriter>>();
  NiceMock<ThreadLocal::MockInstance> tls_;
  UdpStatsdSink sink(tls_, writer_ptr, true);

  std::vector<Stats::Tag> tags = {Stats::Tag{"key1", "value1"}, Stats::Tag{"key2", "value2"}};

  NiceMock<Stats::MockHistogram> items;
  items.name_ = "items";
  items.unit_ = Stats::Histogram::Unit::Unspecified;
  items.setTags(tags);

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.items:1|ms|#key1:value1,key2:value2"));
  sink.onHistogramComplete(items, 1);

  NiceMock<Stats::MockHistogram> information;
  information.name_ = "information";
  information.unit_ = Stats::Histogram::Unit::Bytes;
  information.setTags(tags);

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.information:2|ms|#key1:value1,key2:value2"));
  sink.onHistogramComplete(information, 2);

  NiceMock<Stats::MockHistogram> duration_micro;
  duration_micro.name_ = "duration";
  duration_micro.unit_ = Stats::Histogram::Unit::Microseconds;
  duration_micro.setTags(tags);

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.duration:3|ms|#key1:value1,key2:value2"));
  sink.onHistogramComplete(duration_micro, 3);

  NiceMock<Stats::MockHistogram> duration_milli;
  duration_milli.name_ = "duration";
  duration_milli.unit_ = Stats::Histogram::Unit::Milliseconds;
  duration_milli.setTags(tags);

  EXPECT_CALL(*std::dynamic_pointer_cast<NiceMock<MockWriter>>(writer_ptr),
              write("envoy.duration:4|ms|#key1:value1,key2:value2"));
  sink.onHistogramComplete(duration_milli, 4);

  tls_.shutdownThread();
}

} // namespace
} // namespace Statsd
} // namespace Common
} // namespace StatSinks
} // namespace Extensions
} // namespace Envoy
