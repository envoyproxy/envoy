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
  PerSocketTapperPtr createPerSocketTapper(const Network::Connection& connection) override {
    return PerSocketTapperPtr{createPerSocketTapper_(connection)};
  }

  Extensions::Common::Tap::PerTapSinkHandleManagerPtr
  createPerTapSinkHandleManager(uint64_t trace_id) override {
    return Extensions::Common::Tap::PerTapSinkHandleManagerPtr{
        createPerTapSinkHandleManager_(trace_id)};
  }

  MOCK_METHOD(PerSocketTapper*, createPerSocketTapper_, (const Network::Connection& connection));
  MOCK_METHOD(Extensions::Common::Tap::PerTapSinkHandleManager*, createPerTapSinkHandleManager_,
              (uint64_t trace_id));
  MOCK_METHOD(uint32_t, maxBufferedRxBytes, (), (const));
  MOCK_METHOD(uint32_t, maxBufferedTxBytes, (), (const));
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
    tapper_ = std::make_unique<PerSocketTapperImpl>(config_, connection_);
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

} // namespace
} // namespace Tap
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
