#include "source/common/network/address_impl.h"
#include "source/extensions/transport_sockets/tap/tap_config_impl.h"

#include "test/extensions/common/tap/common.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/simulated_time_system.h"

using testing::_;
using testing::ByMove;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tap {
namespace {

namespace TapCommon = Extensions::Common::Tap;

class MockSocketTapConfig : public SocketTapConfig {
public:
  PerSocketTapperPtr createPerSocketTapper(
      const envoy::extensions::transport_sockets::tap::v3::SocketTapConfig& tap_config,
      const TransportTapStats& stats, const Network::Connection& connection) override {
    return PerSocketTapperPtr{createPerSocketTapper_(tap_config, stats, connection)};
  }

  Extensions::Common::Tap::PerTapSinkHandleManagerPtr
  createPerTapSinkHandleManager(uint64_t trace_id) override {
    return Extensions::Common::Tap::PerTapSinkHandleManagerPtr{
        createPerTapSinkHandleManager_(trace_id)};
  }

  MOCK_METHOD(PerSocketTapper*, createPerSocketTapper_,
              (const envoy::extensions::transport_sockets::tap::v3::SocketTapConfig& tap_config,
               const TransportTapStats& stats, const Network::Connection& connection));
  MOCK_METHOD(Extensions::Common::Tap::PerTapSinkHandleManager*, createPerTapSinkHandleManager_,
              (uint64_t trace_id));
  MOCK_METHOD(uint32_t, maxBufferedRxBytes, (), (const));
  MOCK_METHOD(uint32_t, maxBufferedTxBytes, (), (const));
  MOCK_METHOD(uint32_t, minStreamedSentBytes, (), (const));
  MOCK_METHOD(Extensions::Common::Tap::Matcher::MatchStatusVector, createMatchStatusVector, (),
              (const));
  MOCK_METHOD(const Extensions::Common::Tap::Matcher&, rootMatcher, (), (const));
  MOCK_METHOD(bool, streaming, (), (const));
  MOCK_METHOD(TimeSource&, timeSource, (), (const));
};

class PerSocketTapperImplTest : public testing::Test {
public:
  void setup(bool streaming) {
    connection_.stream_info_.downstream_connection_info_provider_->setLocalAddress(
        std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 1000));
    ON_CALL(connection_, id()).WillByDefault(Return(1));
    EXPECT_CALL(*config_, createPerTapSinkHandleManager_(1)).WillOnce(Return(sink_manager_));
    EXPECT_CALL(*config_, createMatchStatusVector())
        .WillOnce(Return(ByMove(TapCommon::Matcher::MatchStatusVector(1))));
    EXPECT_CALL(*config_, rootMatcher()).WillRepeatedly(ReturnRef(matcher_));
    EXPECT_CALL(matcher_, onNewStream(_))
        .WillOnce(Invoke([this](TapCommon::Matcher::MatchStatusVector& statuses) {
          statuses_ = &statuses;
          if (fail_match_) {
            statuses[0].matches_ = false;
          } else {
            statuses[0].matches_ = true;
          }
          statuses[0].might_change_status_ = false;
        }));
    EXPECT_CALL(*config_, streaming()).WillRepeatedly(Return(streaming));
    EXPECT_CALL(*config_, maxBufferedRxBytes()).WillRepeatedly(Return(1024));
    EXPECT_CALL(*config_, maxBufferedTxBytes()).WillRepeatedly(Return(1024));
    EXPECT_CALL(*config_, timeSource()).WillRepeatedly(ReturnRef(time_system_));
    time_system_.setSystemTime(std::chrono::seconds(0));
    if (send_streamed_msg_on_configured_size_) {
      EXPECT_CALL(*config_, minStreamedSentBytes())
          .WillRepeatedly(Return(default_min_buffered_bytes_));
    } else {
      EXPECT_CALL(*config_, minStreamedSentBytes()).WillRepeatedly(Return(0));
    }
    // Only for streaming trace
    tap_config_.set_set_connection_per_event(output_conn_info_per_event_);

    // stats for both streaming and buffered trace
    std::string final_prefix = fmt::format("transport.tap.");
    TransportTapStats stats{
        ALL_TRANSPORT_TAP_STATS(POOL_COUNTER_PREFIX(*stats_store_.rootScope(), final_prefix))};
    if (pegging_counter_) {
      tap_config_.set_stats_prefix("tranTapPrefix");
    }

    tapper_ = std::make_unique<PerSocketTapperImpl>(config_, tap_config_, stats, connection_);
  }

  std::shared_ptr<MockSocketTapConfig> config_{std::make_shared<MockSocketTapConfig>()};
  // Raw pointer, returned via mock to unique_ptr.
  TapCommon::MockPerTapSinkHandleManager* sink_manager_ =
      new TapCommon::MockPerTapSinkHandleManager;
  std::unique_ptr<PerSocketTapperImpl> tapper_;
  std::vector<TapCommon::MatcherPtr> matchers_{1};
  TapCommon::MockMatcher matcher_{matchers_};
  TapCommon::Matcher::MatchStatusVector* statuses_;
  NiceMock<Network::MockConnection> connection_;
  Event::SimulatedTimeSystem time_system_;
  bool fail_match_{};
  // Add transport configurations
  envoy::extensions::transport_sockets::tap::v3::SocketTapConfig tap_config_;
  bool output_conn_info_per_event_{false};
  bool pegging_counter_{false};
  Stats::IsolatedStoreImpl stats_store_;
  bool send_streamed_msg_on_configured_size_{false};
  unsigned int default_min_buffered_bytes_ = 20;
};

// Verify the full streaming flow.
TEST_F(PerSocketTapperImplTest, StreamingFlow) {
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  connection:
    local_address:
      socket_address:
        address: 127.0.0.1
        port_value: 1000
    remote_address:
      socket_address:
        address: 10.0.0.3
        port_value: 50000
)EOF")));
  setup(true);

  InSequence s;

  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  event:
    timestamp: 1970-01-01T00:00:00Z
    read:
      data:
        as_bytes: aGVsbG8=
    seq_num: 1
)EOF")));
  tapper_->onRead(Buffer::OwnedImpl("hello"), 5);

  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  event:
    timestamp: 1970-01-01T00:00:01Z
    write:
      data:
        as_bytes: d29ybGQ=
      end_stream: true
    seq_num: 6
)EOF")));
  time_system_.setSystemTime(std::chrono::seconds(1));
  tapper_->onWrite(Buffer::OwnedImpl("world"), 5, true);

  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  event:
    timestamp: 1970-01-01T00:00:02Z
    closed: {}
    seq_num: 12
)EOF")));
  time_system_.setSystemTime(std::chrono::seconds(2));
  tapper_->closeSocket(Network::ConnectionEvent::RemoteClose);
}

TEST_F(PerSocketTapperImplTest, NonMatchingFlow) {
  fail_match_ = true;
  setup(true);

  EXPECT_CALL(*sink_manager_, submitTrace_(_)).Times(0);
  time_system_.setSystemTime(std::chrono::seconds(2));
  tapper_->closeSocket(Network::ConnectionEvent::RemoteClose);
}

// Verify the full streaming flow.
TEST_F(PerSocketTapperImplTest, StreamingFlowOutputConnInfoPerEvent) {
  // keep the original value
  bool local_output_conn_info_per_event = output_conn_info_per_event_;
  output_conn_info_per_event_ = true;
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  connection:
    local_address:
      socket_address:
        address: 127.0.0.1
        port_value: 1000
    remote_address:
      socket_address:
        address: 10.0.0.3
        port_value: 50000
)EOF")));
  setup(true);

  InSequence s;

  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  event:
    timestamp: 1970-01-01T00:00:00Z
    read:
      data:
        as_bytes: aGVsbG8=
    connection:
      local_address:
        socket_address:
          address: 127.0.0.1
          port_value: 1000
      remote_address:
        socket_address:
          address: 10.0.0.3
          port_value: 50000
    seq_num: 1
)EOF")));
  tapper_->onRead(Buffer::OwnedImpl("hello"), 5);

  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  event:
    timestamp: 1970-01-01T00:00:01Z
    write:
      data:
        as_bytes: d29ybGQ=
      end_stream: true
    connection:
      local_address:
        socket_address:
          address: 127.0.0.1
          port_value: 1000
      remote_address:
        socket_address:
          address: 10.0.0.3
          port_value: 50000
    seq_num: 6
)EOF")));
  time_system_.setSystemTime(std::chrono::seconds(1));
  tapper_->onWrite(Buffer::OwnedImpl("world"), 5, true);

  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  event:
    timestamp: 1970-01-01T00:00:02Z
    closed: {}
    connection:
      local_address:
        socket_address:
          address: 127.0.0.1
          port_value: 1000
      remote_address:
        socket_address:
          address: 10.0.0.3
          port_value: 50000
    seq_num: 12
)EOF")));
  time_system_.setSystemTime(std::chrono::seconds(2));
  tapper_->closeSocket(Network::ConnectionEvent::RemoteClose);
  // restore the value
  output_conn_info_per_event_ = local_output_conn_info_per_event;
}

// Verify the full streaming flow for submiting tapped message on all cases
// When send_streamed_msg_on_configured_size_ is false
TEST_F(PerSocketTapperImplTest, StreamingFlowWhenSendStreamedMsgIsFalse) {
  // Keep the original value.
  bool local_output_conn_info_per_event = output_conn_info_per_event_;
  output_conn_info_per_event_ = true;
  bool local_pegging_counter = pegging_counter_;
  pegging_counter_ = true;

  // Submit when the transport socket is created
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  connection:
    local_address:
      socket_address:
        address: 127.0.0.1
        port_value: 1000
    remote_address:
      socket_address:
        address: 10.0.0.3
        port_value: 50000
)EOF")));
  setup(true);

  InSequence s;

  // Submit when the transport socket is gotten read event
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  event:
    timestamp: 1970-01-01T00:00:00Z
    read:
      data:
        as_bytes: aGVsbG8=
    connection:
      local_address:
        socket_address:
          address: 127.0.0.1
          port_value: 1000
      remote_address:
        socket_address:
          address: 10.0.0.3
          port_value: 50000
    seq_num: 1
)EOF")));
  tapper_->onRead(Buffer::OwnedImpl("hello"), 5);

  // Submit when the transport socket is gotten write event
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  event:
    timestamp: 1970-01-01T00:00:01Z
    write:
      data:
        as_bytes: d29ybGQ=
      end_stream: true
    connection:
      local_address:
        socket_address:
          address: 127.0.0.1
          port_value: 1000
      remote_address:
        socket_address:
          address: 10.0.0.3
          port_value: 50000
    seq_num: 6
)EOF")));
  time_system_.setSystemTime(std::chrono::seconds(1));
  tapper_->onWrite(Buffer::OwnedImpl("world"), 5, true);

  // Submit when the transport socket is gotten close event
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  event:
    timestamp: 1970-01-01T00:00:02Z
    closed: {}
    connection:
      local_address:
        socket_address:
          address: 127.0.0.1
          port_value: 1000
      remote_address:
        socket_address:
          address: 10.0.0.3
          port_value: 50000
    seq_num: 12
)EOF")));
  time_system_.setSystemTime(std::chrono::seconds(2));
  tapper_->closeSocket(Network::ConnectionEvent::RemoteClose);

  // Restore the value
  output_conn_info_per_event_ = local_output_conn_info_per_event;
  pegging_counter_ = local_pegging_counter;
}

// Verify the full streaming flow for submiting tapped message on all cases.
// When the send_streamed_msg_on_configured_size_ is True.
TEST_F(PerSocketTapperImplTest, StreamingFlowWhenSendStreamedMsgIsTrue) {
  // Keep the original value.
  bool local_output_conn_info_per_event = output_conn_info_per_event_;
  output_conn_info_per_event_ = true;
  bool local_pegging_counter = pegging_counter_;
  pegging_counter_ = true;
  bool local_send_streamed_msg_on_configured_size_ = send_streamed_msg_on_configured_size_;
  send_streamed_msg_on_configured_size_ = true;
  bool local_default_min_buffered_bytes = default_min_buffered_bytes_;

  // Submit when the transport socket is created.
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  connection:
    local_address:
      socket_address:
        address: 127.0.0.1
        port_value: 1000
    remote_address:
      socket_address:
        address: 10.0.0.3
        port_value: 50000
)EOF")));
  setup(true);

  InSequence s;

  // Submit the single read event.
  default_min_buffered_bytes_ = 50;
  EXPECT_CALL(*config_, minStreamedSentBytes()).WillRepeatedly(Return(default_min_buffered_bytes_));

  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  events:
    events:
    - timestamp: 1970-01-01T00:00:00Z
      read:
        data:
          as_bytes: VGVzdCB0cmFuc3BvcnQgc29ja2V0IHRhcCBidWZmZXJlZCBkYXRhIG9uUmVhZCBzdWJtaXQ=
      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 1
)EOF")));
  tapper_->onRead(Buffer::OwnedImpl("Test transport socket tap buffered data onRead submit"), 53);

  // Submit the single write event.
  EXPECT_CALL(*config_, minStreamedSentBytes()).WillRepeatedly(Return(default_min_buffered_bytes_));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  events:
    events:
    - timestamp: 1970-01-01T00:00:01Z
      write:
        data:
          as_bytes: VGVzdCB0cmFuc3BvcnQgc29ja2V0IHRhcCBidWZmZXJlZCBkYXRhIG9uV3JpdGUgc3VibWl0
        end_stream: true
      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 54
)EOF")));
  time_system_.setSystemTime(std::chrono::seconds(1));
  tapper_->onWrite(Buffer::OwnedImpl("Test transport socket tap buffered data onWrite submit"), 54,
                   true);

  // Submit when the transport socket is gotten close event.
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  events:
    events:
    - timestamp: 1970-01-01T00:00:02Z
      closed: {}
      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 109
)EOF")));
  time_system_.setSystemTime(std::chrono::seconds(2));
  tapper_->closeSocket(Network::ConnectionEvent::RemoteClose);

  // Restore the value.
  output_conn_info_per_event_ = local_output_conn_info_per_event;
  pegging_counter_ = local_pegging_counter;
  send_streamed_msg_on_configured_size_ = local_send_streamed_msg_on_configured_size_;
  default_min_buffered_bytes_ = local_default_min_buffered_bytes;
}

// Verify the full streaming flow for submiting tapped message on all cases.
// When the send_streamed_msg_on_configured_size_ is True and two read events.
// and submitted because aged duration is reached threshold.
TEST_F(PerSocketTapperImplTest, StreamingFlowWhenSendStreamedMsgIsTrueTwoReadEvents) {
  // Keep the original value.
  bool local_output_conn_info_per_event = output_conn_info_per_event_;
  output_conn_info_per_event_ = true;
  bool local_pegging_counter = pegging_counter_;
  pegging_counter_ = true;
  bool local_send_streamed_msg_on_configured_size_ = send_streamed_msg_on_configured_size_;
  send_streamed_msg_on_configured_size_ = true;
  bool local_default_min_buffered_bytes = default_min_buffered_bytes_;
  default_min_buffered_bytes_ = 120;

  // Submit when the transport socket is created.
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  connection:
    local_address:
      socket_address:
        address: 127.0.0.1
        port_value: 1000
    remote_address:
      socket_address:
        address: 10.0.0.3
        port_value: 50000
)EOF")));
  setup(true);

  InSequence s;

  // Store the read event.
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  events:
    events:
    - timestamp: 1970-01-01T00:00:00Z
      read:
        data:
          as_bytes: VGVzdCB0cmFuc3BvcnQgc29ja2V0IHRhcCBidWZmZXJlZCBkYXRhIG9uUmVhZCBzdWJtaXQ=
      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 1
    - timestamp: 1970-01-01T00:00:15Z
      read:
        data:
          as_bytes: VGVzdCB0cmFuc3BvcnQgc29ja2V0IHRhcCBidWZmZXJlZCBkYXRhIG9uUmVhZCBzdWJtaXQ=
      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 54
)EOF")));
  tapper_->onRead(Buffer::OwnedImpl("Test transport socket tap buffered data onRead submit"), 53);
  time_system_.setSystemTime(std::chrono::seconds(15));
  tapper_->onRead(Buffer::OwnedImpl("Test transport socket tap buffered data onRead submit"), 53);

  // Submit when the transport socket is gotten close event.
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  events:
    events:
    - timestamp: 1970-01-01T00:00:02Z
      closed: {}
      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 108
)EOF")));
  time_system_.setSystemTime(std::chrono::seconds(2));
  tapper_->closeSocket(Network::ConnectionEvent::RemoteClose);

  // Restore the value.
  output_conn_info_per_event_ = local_output_conn_info_per_event;
  pegging_counter_ = local_pegging_counter;
  send_streamed_msg_on_configured_size_ = local_send_streamed_msg_on_configured_size_;
  default_min_buffered_bytes_ = local_default_min_buffered_bytes;
}

// Verify the full streaming flow for submiting tapped message on all cases
// When the send_streamed_msg_on_configured_size_ is True and two write events
TEST_F(PerSocketTapperImplTest, StreamingFlowWhenSendStreamedMsgIsTruetwoWriteEvents) {
  // Keep the original value.
  bool local_output_conn_info_per_event = output_conn_info_per_event_;
  output_conn_info_per_event_ = true;
  bool local_pegging_counter = pegging_counter_;
  pegging_counter_ = true;
  bool local_send_streamed_msg_on_configured_size_ = send_streamed_msg_on_configured_size_;
  send_streamed_msg_on_configured_size_ = true;
  bool local_default_min_buffered_bytes = default_min_buffered_bytes_;
  default_min_buffered_bytes_ = 120;

  // Submit when the transport socket is created.
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  connection:
    local_address:
      socket_address:
        address: 127.0.0.1
        port_value: 1000
    remote_address:
      socket_address:
        address: 10.0.0.3
        port_value: 50000
)EOF")));
  setup(true);

  InSequence s;

  // Submit when the aged duration is equal 12.
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  events:
    events:
    - timestamp: 1970-01-01T00:00:00Z
      write:
        data:
          as_bytes: VGVzdCB0cmFuc3BvcnQgc29ja2V0IHRhcCBidWZmZXJlZCBkYXRhIG9uV3JpdGUgc3VibWl0
        end_stream: true
      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 1
    - timestamp: 1970-01-01T00:00:15Z
      write:
        data:
          as_bytes: VGVzdCB0cmFuc3BvcnQgc29ja2V0IHRhcCBidWZmZXJlZCBkYXRhIG9uV3JpdGUgc3VibWl0
        end_stream: true
      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 55
)EOF")));
  tapper_->onWrite(Buffer::OwnedImpl("Test transport socket tap buffered data onWrite submit"), 54,
                   true);
  time_system_.setSystemTime(std::chrono::seconds(15));
  tapper_->onWrite(Buffer::OwnedImpl("Test transport socket tap buffered data onWrite submit"), 54,
                   true);

  // Submit when the transport socket is gotten close event.
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  events:
    events:
    - timestamp: 1970-01-01T00:00:02Z
      closed: {}
      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 110
)EOF")));
  time_system_.setSystemTime(std::chrono::seconds(2));
  tapper_->closeSocket(Network::ConnectionEvent::RemoteClose);

  // Restore the value.
  output_conn_info_per_event_ = local_output_conn_info_per_event;
  pegging_counter_ = local_pegging_counter;
  send_streamed_msg_on_configured_size_ = local_send_streamed_msg_on_configured_size_;
  default_min_buffered_bytes_ = local_default_min_buffered_bytes;
}

// All data are submitted in close event
TEST_F(PerSocketTapperImplTest, StreamingFlowWhenSendStreamedMsgIsTrueInCloseEvents) {
  // Keep the original value.
  bool local_output_conn_info_per_event = output_conn_info_per_event_;
  output_conn_info_per_event_ = true;
  bool local_pegging_counter = pegging_counter_;
  pegging_counter_ = true;
  bool local_send_streamed_msg_on_configured_size_ = send_streamed_msg_on_configured_size_;
  send_streamed_msg_on_configured_size_ = true;
  bool local_default_min_buffered_bytes = default_min_buffered_bytes_;
  default_min_buffered_bytes_ = 128;

  // Submit when the transport socket is created.
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  connection:
    local_address:
      socket_address:
        address: 127.0.0.1
        port_value: 1000
    remote_address:
      socket_address:
        address: 10.0.0.3
        port_value: 50000
)EOF")));
  setup(true);

  InSequence s;

  time_system_.setSystemTime(std::chrono::seconds(1));
  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_streamed_trace_segment:
  trace_id: 1
  events:
    events:
    - timestamp: 1970-01-01T00:00:01Z
      read:
        data:
          as_bytes: VGVzdCB0cmFuc3BvcnQgc29ja2V0IHRhcCBidWZmZXJlZCBkYXRhIG9uUmVhZCBzdWJtaXQ=

      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 1
    - timestamp: 1970-01-01T00:00:02Z
      write:
        data:
          as_bytes: VGVzdCB0cmFuc3BvcnQgc29ja2V0IHRhcCBidWZmZXJlZCBkYXRhIG9uV3JpdGUgc3VibWl0

        end_stream: true
      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 54
    - timestamp: 1970-01-01T00:00:03Z
      closed: {}
      connection:
        local_address:
          socket_address:
            address: 127.0.0.1
            port_value: 1000
        remote_address:
          socket_address:
            address: 10.0.0.3
            port_value: 50000
      seq_num: 109
)EOF")));
  tapper_->onRead(Buffer::OwnedImpl("Test transport socket tap buffered data onRead submit"), 53);
  time_system_.setSystemTime(std::chrono::seconds(2));
  tapper_->onWrite(Buffer::OwnedImpl("Test transport socket tap buffered data onWrite submit"), 54,
                   true);

  time_system_.setSystemTime(std::chrono::seconds(3));
  tapper_->closeSocket(Network::ConnectionEvent::RemoteClose);

  // Restore the value.
  output_conn_info_per_event_ = local_output_conn_info_per_event;
  pegging_counter_ = local_pegging_counter;
  send_streamed_msg_on_configured_size_ = local_send_streamed_msg_on_configured_size_;
  default_min_buffered_bytes_ = local_default_min_buffered_bytes;
}

// Verify the full buffered flow for submit data in close event.
TEST_F(PerSocketTapperImplTest, BufferedFlow) {
  // Keep the original value
  bool local_pegging_counter = pegging_counter_;
  pegging_counter_ = true;
  bool local_sending_tapped_msg_on_configured_size = send_streamed_msg_on_configured_size_;
  send_streamed_msg_on_configured_size_ = true;
  bool local_default_min_buffered_bytes = default_min_buffered_bytes_;
  default_min_buffered_bytes_ = 30;

  setup(false);
  // InSequence s;

  EXPECT_CALL(*sink_manager_, submitTrace_(TraceEqual(
                                  R"EOF(
socket_buffered_trace:
  trace_id: 1
  connection:
    local_address:
      socket_address:
        address: 127.0.0.1
        port_value: 1000
    remote_address:
      socket_address:
        address: 10.0.0.3
        port_value: 50000
  events:
  - timestamp: 1970-01-01T00:00:00Z
    read:
      data:
        as_bytes: aGVsbG8=
  - timestamp: 1970-01-01T00:00:01Z
    write:
      data:
        as_bytes: d29ybGQ=
      end_stream: true
)EOF")));
  tapper_->onRead(Buffer::OwnedImpl("hello"), 5);

  // Call onWrite after one seconds.
  time_system_.setSystemTime(std::chrono::seconds(1));
  tapper_->onWrite(Buffer::OwnedImpl("world"), 5, true);

  // All buffered data is submitted.
  time_system_.setSystemTime(std::chrono::seconds(2));
  tapper_->closeSocket(Network::ConnectionEvent::RemoteClose);

  // Restore the value.
  pegging_counter_ = local_pegging_counter;
  send_streamed_msg_on_configured_size_ = local_sending_tapped_msg_on_configured_size;
  default_min_buffered_bytes_ = local_default_min_buffered_bytes;
}

} // namespace
} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
