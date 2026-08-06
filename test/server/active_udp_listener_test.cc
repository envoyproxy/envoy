#include <memory>

#include "envoy/network/filter.h"
#include "envoy/network/listener.h"

#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/common/network/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/server/active_udp_listener.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Server {
namespace {

class MockUdpConnectionHandler : public Network::UdpConnectionHandler,
                                 public Network::MockConnectionHandler {
public:
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Network::UdpListenerCallbacksOptRef, getUdpListenerCallbacks,
              (uint64_t listener_tag, const Network::Address::Instance& address));
};

class TestActiveRawUdpListener : public ActiveRawUdpListener {
public:
  TestActiveRawUdpListener(uint32_t worker_index, uint32_t concurrency,
                           Network::UdpConnectionHandler& parent,
                           Network::SocketSharedPtr listen_socket_ptr,
                           Event::Dispatcher& dispatcher, Network::ListenerConfig& config)
      : ActiveRawUdpListener(worker_index, concurrency, parent, listen_socket_ptr, dispatcher,
                             config),
        destination_(worker_index) {}
  uint32_t destination(const Network::UdpRecvData&) const override { return destination_; }

  uint32_t destination_;
};

class ActiveUdpListenerTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              protected Logger::Loggable<Logger::Id::main> {
public:
  ActiveUdpListenerTest()
      : version_(GetParam()), local_address_(Network::Test::getCanonicalLoopbackAddress(version_)) {
  }

  void setup(uint32_t concurrency = 1,
             std::chrono::milliseconds flow_idle_timeout = std::chrono::milliseconds(60000)) {
    udp_listener_config_ = std::make_unique<NiceMock<Network::MockUdpListenerConfig>>(2);
    *udp_listener_config_->config_.mutable_flow_idle_timeout() =
        ProtobufUtil::TimeUtil::MillisecondsToDuration(flow_idle_timeout.count());
    ON_CALL(conn_handler_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    EXPECT_CALL(conn_handler_, statPrefix()).WillRepeatedly(ReturnRef(listener_stat_prefix_));

    listen_socket_ =
        std::make_shared<Network::UdpListenSocket>(local_address_, nullptr, /*bind*/ true);
    listen_socket_->addOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
    listen_socket_->addOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
    ASSERT_TRUE(Network::Socket::applyOptions(listen_socket_->options(), *listen_socket_,
                                              envoy::config::core::v3::SocketOption::STATE_BOUND));

    ON_CALL(*static_cast<Network::MockListenSocketFactory*>(
                listener_config_.socket_factories_[0].get()),
            getListenSocket(_))
        .WillByDefault(Return(listen_socket_));

    // Use UdpGsoBatchWriter to perform non-batched writes for the purpose of this test, if it is
    // supported.
    EXPECT_CALL(listener_config_, udpListenerConfig())
        .WillRepeatedly(Return(Network::UdpListenerConfigOptRef(*udp_listener_config_)));
    EXPECT_CALL(listener_config_, listenerScope()).WillRepeatedly(ReturnRef(scope_));
    EXPECT_CALL(listener_config_, filterChainFactory());
    ON_CALL(*udp_listener_config_, packetWriterFactory())
        .WillByDefault(ReturnRef(udp_packet_writer_factory_));
    ON_CALL(udp_packet_writer_factory_, createUdpPacketWriter(_, _, _, _))
        .WillByDefault(
            Invoke([&](Network::IoHandle& io_handle, Stats::Scope& scope, Envoy::Event::Dispatcher&,
                       absl::AnyInvocable<void() &&>) -> Network::UdpPacketWriterPtr {
#if UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT
              return std::make_unique<Quic::UdpGsoBatchWriter>(io_handle, scope);
#else
              UNREFERENCED_PARAMETER(scope);
              return std::make_unique<Network::UdpDefaultWriter>(io_handle);
#endif
            }));

    EXPECT_CALL(cb_.udp_listener_, onDestroy());

    active_listener_ = std::make_unique<TestActiveRawUdpListener>(
        0, concurrency, conn_handler_, listen_socket_, dispatcher_, listener_config_);
  }

  Network::UdpRecvData makeRecvData(uint32_t peer_port) {
    Network::UdpRecvData data;
    data.addresses_.local_ = listen_socket_->connectionInfoProvider().localAddress();
    data.addresses_.peer_ = Network::Utility::getAddressWithPort(*local_address_, peer_port);
    return data;
  }

  std::string listener_stat_prefix_{"listener_stat_prefix"};
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};
  NiceMock<MockUdpConnectionHandler> conn_handler_;
  Network::Address::IpVersion version_;
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::SocketSharedPtr listen_socket_;
  NiceMock<Network::MockListenSocketFactory> socket_factory_;
  Stats::IsolatedStoreImpl store_;
  Stats::Scope& scope_{*store_.rootScope()};
  std::unique_ptr<NiceMock<Network::MockUdpListenerConfig>> udp_listener_config_;
  NiceMock<Network::MockUdpPacketWriterFactory> udp_packet_writer_factory_;
  Network::MockListenerConfig listener_config_;
  std::unique_ptr<TestActiveRawUdpListener> active_listener_;
  NiceMock<Network::MockUdpReadFilterCallbacks> cb_;
};

INSTANTIATE_TEST_SUITE_P(ActiveUdpListenerTests, ActiveUdpListenerTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ActiveUdpListenerTest, MultipleFiltersOnData) {
  setup();

  auto* test_filter = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter, onData(_))
      .WillOnce(Invoke([](Network::UdpRecvData&) -> Network::FilterStatus {
        return Network::FilterStatus::Continue;
      }));
  auto* test_filter2 = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter2, onData(_))
      .WillOnce(Invoke([](Network::UdpRecvData&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));

  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter});
  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter2});

  active_listener_->onDataWorker(makeRecvData(1000));
}

TEST_P(ActiveUdpListenerTest, MultipleFiltersOnDataStopIteration) {
  setup();

  auto* test_filter = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter, onData(_))
      .WillOnce(Invoke([](Network::UdpRecvData&) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));
  auto* test_filter2 = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter2, onData(_)).Times(0);

  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter});
  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter2});

  active_listener_->onDataWorker(makeRecvData(1000));
}

TEST_P(ActiveUdpListenerTest, MultipleFiltersOnReceiveError) {
  setup();

  auto* test_filter = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter, onReceiveError(_))
      .WillOnce(Invoke([](Api::IoError::IoErrorCode) -> Network::FilterStatus {
        return Network::FilterStatus::Continue;
      }));
  auto* test_filter2 = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter2, onReceiveError(_))
      .WillOnce(Invoke([](Api::IoError::IoErrorCode) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));

  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter});
  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter2});

  active_listener_->onReceiveError(Api::IoError::IoErrorCode::UnknownError);
}

TEST_P(ActiveUdpListenerTest, MultipleFiltersOnReceiveErrorStopIteration) {
  setup();

  auto* test_filter = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter, onReceiveError(_))
      .WillOnce(Invoke([](Api::IoError::IoErrorCode) -> Network::FilterStatus {
        return Network::FilterStatus::StopIteration;
      }));
  auto* test_filter2 = new NiceMock<Network::MockUdpListenerReadFilter>(cb_);
  EXPECT_CALL(*test_filter2, onReceiveError(_)).Times(0);

  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter});
  active_listener_->addReadFilter(Network::UdpListenerReadFilterPtr{test_filter2});

  active_listener_->onReceiveError(Api::IoError::IoErrorCode::UnknownError);
}

TEST_P(ActiveUdpListenerTest, FlowIdleTimerLifecycle) {
  const auto flow_idle_timeout = std::chrono::milliseconds(5);
  auto* sweep_timer = new Event::MockTimer(&dispatcher_);
  setup(1, flow_idle_timeout);

  MonotonicTime now{};
  ON_CALL(dispatcher_, approximateMonotonicTime()).WillByDefault(ReturnPointee(&now));

  auto test_filter = std::make_unique<NiceMock<Network::MockUdpListenerReadFilter>>(cb_);
  EXPECT_CALL(*test_filter, onData(_)).Times(3);
  active_listener_->addReadFilter(std::move(test_filter));
  EXPECT_CALL(*sweep_timer, enableTimer(flow_idle_timeout, _)).Times(3);

  // The first flow arms the sweep timer.
  active_listener_->onData(makeRecvData(1000));

  // A refreshed flow survives a sweep, which rearms the timer.
  now += std::chrono::milliseconds(3);
  active_listener_->onData(makeRecvData(1000));
  now += std::chrono::milliseconds(3);
  sweep_timer->invokeCallback();

  // An idle flow is erased and the sweep stops; the next packet starts over.
  now += std::chrono::milliseconds(6);
  sweep_timer->invokeCallback();
  active_listener_->onData(makeRecvData(1000));
}

TEST_P(ActiveUdpListenerTest, HotRestartShutdownForwardsUnknownFlows) {
  auto* sweep_timer = new Event::MockTimer(&dispatcher_);
  setup();

  MonotonicTime now{};
  ON_CALL(dispatcher_, approximateMonotonicTime()).WillByDefault(ReturnPointee(&now));

  auto test_filter = std::make_unique<NiceMock<Network::MockUdpListenerReadFilter>>(cb_);
  EXPECT_CALL(*test_filter, onData(_)).Times(2);
  active_listener_->addReadFilter(std::move(test_filter));
  EXPECT_CALL(*sweep_timer, enableTimer(_, _)).Times(AnyNumber());

  active_listener_->onData(makeRecvData(1000));

  Network::MockNonDispatchedUdpPacketHandler packet_handler;
  Network::ExtraShutdownListenerOptions options;
  options.non_dispatched_udp_packet_handler_ = packet_handler;
  active_listener_->shutdownListener(options);
  EXPECT_NE(active_listener_->listener(), nullptr);

  // Known flow is still served locally.
  active_listener_->onData(makeRecvData(1000));

  // Unknown flow is forwarded to the child instance without creating local flow state,
  // so a repeated packet is forwarded again.
  EXPECT_CALL(packet_handler, handle(0, _)).Times(3);
  active_listener_->onData(makeRecvData(2000));
  active_listener_->onData(makeRecvData(2000));

  // Flow idled out by the sweep counts as unknown.
  now += std::chrono::hours(1);
  sweep_timer->invokeCallback();
  active_listener_->onData(makeRecvData(1000));
}

TEST_P(ActiveUdpListenerTest, RegularShutdownDestroysListener) {
  setup();

  auto test_filter = std::make_unique<NiceMock<Network::MockUdpListenerReadFilter>>(cb_);
  EXPECT_CALL(*test_filter, onData(_)).Times(0);
  active_listener_->addReadFilter(std::move(test_filter));

  active_listener_->shutdownListener({});
  EXPECT_EQ(active_listener_->listener(), nullptr);

  // Packets after teardown are dropped.
  active_listener_->onData(makeRecvData(1000));
}

} // namespace
} // namespace Server
} // namespace Envoy
