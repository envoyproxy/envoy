#include <memory>

#include "envoy/network/filter.h"
#include "envoy/network/listener.h"

#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/socket_option_factory.h"
#include "source/common/network/udp_packet_writer_handler_impl.h"
#include "source/server/active_udp_listener.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Server {
namespace {

class MockUdpConnectionHandler : public Network::UdpConnectionHandler,
                                 public Network::MockConnectionHandler {
public:
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
  MOCK_METHOD(Network::UdpListenerCallbacksOptRef, getUdpListenerCallbacks,
              (uint64_t listener_tag));
};

class ActiveUdpListenerTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              protected Logger::Loggable<Logger::Id::main> {
public:
  ActiveUdpListenerTest()
      : version_(GetParam()), local_address_(Network::Test::getCanonicalLoopbackAddress(version_)) {
  }

  void SetUp() override {
    ON_CALL(conn_handler_, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
    EXPECT_CALL(conn_handler_, statPrefix()).WillRepeatedly(ReturnRef(listener_stat_prefix_));

    listen_socket_ =
        std::make_shared<Network::UdpListenSocket>(local_address_, nullptr, /*bind*/ true);
    listen_socket_->addOptions(Network::SocketOptionFactory::buildIpPacketInfoOptions());
    listen_socket_->addOptions(Network::SocketOptionFactory::buildRxQueueOverFlowOptions());
    ASSERT_TRUE(Network::Socket::applyOptions(listen_socket_->options(), *listen_socket_,
                                              envoy::config::core::v3::SocketOption::STATE_BOUND));

    ON_CALL(socket_factory_, getListenSocket(_)).WillByDefault(Return(listen_socket_));
    EXPECT_CALL(listener_config_, listenSocketFactory()).WillRepeatedly(ReturnRef(socket_factory_));

    // Use UdpGsoBatchWriter to perform non-batched writes for the purpose of this test, if it is
    // supported.
    EXPECT_CALL(listener_config_, udpListenerConfig())
        .WillRepeatedly(Return(Network::UdpListenerConfigOptRef(udp_listener_config_)));
    EXPECT_CALL(listener_config_, listenerScope()).WillRepeatedly(ReturnRef(scope_));
    EXPECT_CALL(listener_config_, filterChainFactory());
    ON_CALL(udp_listener_config_, packetWriterFactory())
        .WillByDefault(ReturnRef(udp_packet_writer_factory_));
    ON_CALL(udp_packet_writer_factory_, createUdpPacketWriter(_, _))
        .WillByDefault(Invoke(
            [&](Network::IoHandle& io_handle, Stats::Scope& scope) -> Network::UdpPacketWriterPtr {
#if UDP_GSO_BATCH_WRITER_COMPILETIME_SUPPORT
              return std::make_unique<Quic::UdpGsoBatchWriter>(io_handle, scope);
#else
              UNREFERENCED_PARAMETER(scope);
              return std::make_unique<Network::UdpDefaultWriter>(io_handle);
#endif
            }));

    EXPECT_CALL(cb_.udp_listener_, onDestroy());
  }

  void setup() {
    active_listener_ =
        std::make_unique<ActiveRawUdpListener>(0, 1, conn_handler_, dispatcher_, listener_config_);
  }

  std::string listener_stat_prefix_{"listener_stat_prefix"};
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};
  NiceMock<MockUdpConnectionHandler> conn_handler_;
  Network::Address::IpVersion version_;
  Network::Address::InstanceConstSharedPtr local_address_;
  Network::SocketSharedPtr listen_socket_;
  NiceMock<Network::MockListenSocketFactory> socket_factory_;
  Stats::IsolatedStoreImpl scope_;
  NiceMock<Network::MockUdpListenerConfig> udp_listener_config_;
  NiceMock<Network::MockUdpPacketWriterFactory> udp_packet_writer_factory_;
  Network::MockListenerConfig listener_config_;
  std::unique_ptr<ActiveRawUdpListener> active_listener_;
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

  Network::UdpRecvData data;
  active_listener_->onDataWorker(std::move(data));
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

  Network::UdpRecvData data;
  active_listener_->onDataWorker(std::move(data));
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

  Network::UdpRecvData data;
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

  Network::UdpRecvData data;
  active_listener_->onReceiveError(Api::IoError::IoErrorCode::UnknownError);
}

} // namespace
} // namespace Server
} // namespace Envoy
