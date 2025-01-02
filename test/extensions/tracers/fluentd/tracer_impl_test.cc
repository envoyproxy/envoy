#include "envoy/common/time.h"
#include "envoy/registry/registry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/tracers/fluentd/config.h"
#include "source/extensions/tracers/fluentd/fluentd_tracer_impl.h"

#include "test/mocks/server/factory_context.h"
#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "msgpack.hpp"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Fluentd {

// Tests adapted from test/extensions/access_loggers/fluentd @ohadvano @wbpcode

class FluentdTracerImplTest : public testing::Test {
public:
  FluentdTracerImplTest()
      : async_client_(new Tcp::AsyncClient::MockAsyncTcpClient()),
        backoff_strategy_(new MockBackOffStrategy()),
        flush_timer_(new Event::MockTimer(&dispatcher_)),
        retry_timer_(new Event::MockTimer(&dispatcher_)) {}

  void init(int buffer_size_bytes = 1, absl::optional<int> max_connect_attempts = absl::nullopt) {
    EXPECT_CALL(*async_client_, setAsyncTcpClientCallbacks(_));
    EXPECT_CALL(*flush_timer_, enableTimer(_, _));

    config_.set_tag(tag_);

    if (max_connect_attempts.has_value()) {
      config_.mutable_retry_options()->mutable_max_connect_attempts()->set_value(
          max_connect_attempts.value());
    }

    config_.mutable_buffer_size_bytes()->set_value(buffer_size_bytes);
    tracer_ = std::make_unique<FluentdTracerImpl>(
        cluster_, Tcp::AsyncTcpClientPtr{async_client_}, dispatcher_, config_,
        BackOffStrategyPtr{backoff_strategy_}, *stats_store_.rootScope(), random_);
  }

  std::string getExpectedMsgpackPayload(int entries_count) {
    msgpack::sbuffer buffer;
    msgpack::packer<msgpack::sbuffer> packer(buffer);
    packer.pack_array(3);
    packer.pack(tag_);
    packer.pack_array(entries_count);
    for (int idx = 0; idx < entries_count; idx++) {
      packer.pack_array(2);
      packer.pack(time_);
      packer.pack_map(data_.size());
      for (const auto& pair : data_) {
        packer.pack(pair.first);
        packer.pack(pair.second);
      }
    }

    std::map<std::string, std::string> option_ = {{"fluent_signal", "2"},
                                                  {"TimeFormat", "DateTime"}};
    packer.pack(option_);
    return std::string(buffer.data(), buffer.size());
  }

  std::string tag_ = "test.tag";
  uint64_t time_ = 123;
  std::map<std::string, std::string> data_ = {{"event", "test"}};
  NiceMock<Upstream::MockThreadLocalCluster> cluster_;
  Tcp::AsyncClient::MockAsyncTcpClient* async_client_;
  MockBackOffStrategy* backoff_strategy_;
  Stats::IsolatedStoreImpl stats_store_;
  Event::MockDispatcher dispatcher_;
  Event::MockTimer* flush_timer_;
  Event::MockTimer* retry_timer_;
  std::unique_ptr<FluentdTracerImpl> tracer_;
  envoy::config::trace::v3::FluentdConfig config_;
  NiceMock<Random::MockRandomGenerator> random_;
};
;

// Fluentd tracer does not write if not connected to upstream.
TEST_F(FluentdTracerImplTest, NoWriteOnTraceIfNotConnectedToUpstream) {
  init();
  EXPECT_CALL(*async_client_, connect()).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Fluentd tracer does not write if the buffer is not full
TEST_F(FluentdTracerImplTest, NoWriteOnTraceIfBufferLimitNotPassed) {
  init(100);
  EXPECT_CALL(*async_client_, connect()).Times(0);
  EXPECT_CALL(*async_client_, connected()).Times(0);
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Fluentd tracer does not write if connection is closed remotely
TEST_F(FluentdTracerImplTest, NoWriteOnTraceIfDisconnectedByRemote) {
  init(1, 1);
  EXPECT_CALL(*flush_timer_, disableTimer());
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    tracer_->onEvent(Network::ConnectionEvent::RemoteClose);
    return true;
  }));

  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Fluentd tracer does not write if connection is closed locally
TEST_F(FluentdTracerImplTest, NoWriteOnTraceIfDisconnectedByLocal) {
  init(1, 1);
  EXPECT_CALL(*flush_timer_, disableTimer());
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    tracer_->onEvent(Network::ConnectionEvent::LocalClose);
    return true;
  }));

  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Traces a single entry
TEST_F(FluentdTracerImplTest, TraceSingleEntry) {
  init(); // Default buffer limit is 0 so single entry should be flushed immediately.
  EXPECT_CALL(*backoff_strategy_, reset());
  EXPECT_CALL(*retry_timer_, disableTimer());
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    tracer_->onEvent(Network::ConnectionEvent::Connected);
    return true;
  }));
  EXPECT_CALL(*async_client_, write(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool end_stream) {
        EXPECT_FALSE(end_stream);
        std::string expected_payload = getExpectedMsgpackPayload(1);
        EXPECT_EQ(expected_payload, buffer.toString());
      }));

  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Traces two entries and flushes them together
TEST_F(FluentdTracerImplTest, TraceTwoEntries) {
  init(12); // First entry is 10 bytes, so first entry should not cause the tracer to flush.

  // First log should not be flushed.
  EXPECT_CALL(*backoff_strategy_, reset());
  EXPECT_CALL(*retry_timer_, disableTimer());
  EXPECT_CALL(*async_client_, connected()).Times(0);
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));

  // Expect second entry to cause all entries to flush.
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Invoke([this]() -> bool {
    tracer_->onEvent(Network::ConnectionEvent::Connected);
    return true;
  }));
  EXPECT_CALL(*async_client_, write(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool end_stream) {
        EXPECT_FALSE(end_stream);
        std::string expected_payload = getExpectedMsgpackPayload(2);
        EXPECT_EQ(expected_payload, buffer.toString());
      }));
  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Tests tracer callbacks
TEST_F(FluentdTracerImplTest, CallbacksTest) {
  init();
  EXPECT_CALL(*async_client_, connect()).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
  EXPECT_NO_THROW(tracer_->onAboveWriteBufferHighWatermark());
  EXPECT_NO_THROW(tracer_->onBelowWriteBufferLowWatermark());
  Buffer::OwnedImpl buffer;
  EXPECT_NO_THROW(tracer_->onData(buffer, false));
}

// Fluentd tracer retries connection on failure and holds entry in buffer
TEST_F(FluentdTracerImplTest, SuccessfulReconnect) {
  init(1, 2);
  EXPECT_CALL(*backoff_strategy_, nextBackOffMs()).WillOnce(Return(1));
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false)).WillOnce(Return(true));
  EXPECT_CALL(*async_client_, connect())
      .WillOnce(Invoke([this]() -> bool {
        EXPECT_CALL(*backoff_strategy_, reset()).Times(0);
        EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(1), _));
        EXPECT_CALL(*retry_timer_, disableTimer()).Times(0);
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }))
      .WillOnce(Invoke([this]() -> bool {
        EXPECT_CALL(*backoff_strategy_, reset());
        EXPECT_CALL(*retry_timer_, enableTimer(_, _)).Times(0);
        EXPECT_CALL(*retry_timer_, disableTimer());
        tracer_->onEvent(Network::ConnectionEvent::Connected);
        return true;
      }));
  EXPECT_CALL(*async_client_, write(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool end_stream) {
        EXPECT_FALSE(end_stream);
        std::string expected_payload = getExpectedMsgpackPayload(1);
        EXPECT_EQ(expected_payload, buffer.toString());
      }));

  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
  retry_timer_->invokeCallback();
}

// Fluentd tracer fails to reconnect and does note write the entry
TEST_F(FluentdTracerImplTest, ReconnectFailure) {
  init(1, 2);

  EXPECT_CALL(*backoff_strategy_, nextBackOffMs()).WillOnce(Return(1));
  EXPECT_CALL(*backoff_strategy_, reset()).Times(0);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(1), _));
  EXPECT_CALL(*retry_timer_, disableTimer()).Times(0);

  EXPECT_CALL(*flush_timer_, disableTimer());
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect())
      .WillOnce(Invoke([this]() -> bool {
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }))
      .WillOnce(Invoke([this]() -> bool {
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }));

  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
  retry_timer_->invokeCallback();
}

// Fluentd tracer reconnects twice
TEST_F(FluentdTracerImplTest, TwoReconnects) {
  init(1, 3);

  EXPECT_CALL(*backoff_strategy_, nextBackOffMs()).WillOnce(Return(1)).WillOnce(Return(1));
  EXPECT_CALL(*backoff_strategy_, reset()).Times(0);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(1), _)).Times(2);
  EXPECT_CALL(*retry_timer_, disableTimer()).Times(0);

  EXPECT_CALL(*flush_timer_, disableTimer());
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect())
      .WillOnce(Invoke([this]() -> bool {
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }))
      .WillOnce(Invoke([this]() -> bool {
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }))
      .WillOnce(Invoke([this]() -> bool {
        tracer_->onEvent(Network::ConnectionEvent::LocalClose);
        return true;
      }));

  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
  retry_timer_->invokeCallback();
  retry_timer_->invokeCallback();
}

// Fluentd tracer retries connection when no healthy upstream is available
TEST_F(FluentdTracerImplTest, RetryOnNoHealthyUpstream) {
  init();

  EXPECT_CALL(*backoff_strategy_, nextBackOffMs()).WillOnce(Return(1));
  EXPECT_CALL(*backoff_strategy_, reset()).Times(0);
  EXPECT_CALL(*retry_timer_, enableTimer(std::chrono::milliseconds(1), _));
  EXPECT_CALL(*retry_timer_, disableTimer()).Times(0);

  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connected()).WillOnce(Return(false));
  EXPECT_CALL(*async_client_, connect()).WillOnce(Return(false));
  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

// Fluentd tracer does not write when the buffer limit is reached
TEST_F(FluentdTracerImplTest, NoWriteOnBufferFull) {
  // Setting the buffer to 0 so new log will be thrown.
  init(0);
  EXPECT_CALL(*async_client_, write(_, _)).Times(0);
  EXPECT_CALL(*async_client_, connect()).Times(0);
  EXPECT_CALL(*async_client_, connected()).Times(0);
  tracer_->trace(
      std::make_unique<Entry>(time_, std::map<std::string, std::string>{{"event", "test"}}));
}

} // namespace Fluentd
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
