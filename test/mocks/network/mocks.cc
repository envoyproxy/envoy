#include "mocks.h"

#include <cstdint>

#include "envoy/buffer/buffer.h"
#include "envoy/server/listener_manager.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/udp_listener_impl.h"
#include "source/common/network/utility.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Envoy {
namespace Network {

MockUdpListenerConfig::MockUdpListenerConfig(uint32_t concurrency)
    : udp_listener_worker_router_(std::make_unique<UdpListenerWorkerRouterImpl>(concurrency)) {
  ON_CALL(*this, listenerWorkerRouter(_)).WillByDefault(ReturnRef(*udp_listener_worker_router_));
  ON_CALL(*this, config()).WillByDefault(ReturnRef(config_));
}
MockUdpListenerConfig::~MockUdpListenerConfig() = default;

MockListenerConfig::MockListenerConfig()
    : socket_(std::make_shared<testing::NiceMock<MockListenSocket>>()) {
  socket_factories_.emplace_back(std::make_unique<MockListenSocketFactory>());
  ON_CALL(*this, filterChainFactory()).WillByDefault(ReturnRef(filter_chain_factory_));
  ON_CALL(*this, listenSocketFactories()).WillByDefault(ReturnRef(socket_factories_));
  ON_CALL(*static_cast<MockListenSocketFactory*>(socket_factories_[0].get()), localAddress())
      .WillByDefault(ReturnRef(socket_->connectionInfoProvider().localAddress()));
  ON_CALL(*static_cast<MockListenSocketFactory*>(socket_factories_[0].get()), getListenSocket(_))
      .WillByDefault(Return(socket_));
  ON_CALL(*this, listenerScope()).WillByDefault(ReturnRef(*store_.rootScope()));
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
  ON_CALL(*this, maxConnectionsToAcceptPerSocketEvent())
      .WillByDefault(Return(Network::DefaultMaxConnectionsToAcceptPerSocketEvent));
  ON_CALL(*this, ignoreGlobalConnLimit()).WillByDefault(Return(false));
  ON_CALL(*this, bindToPort()).WillByDefault(Return(true));
}
MockListenerConfig::~MockListenerConfig() = default;

MockActiveDnsQuery::MockActiveDnsQuery() = default;
MockActiveDnsQuery::~MockActiveDnsQuery() = default;

MockDnsResolver::MockDnsResolver() {
  ON_CALL(*this, resolve(_, _, _)).WillByDefault(Return(&active_query_));
}

MockDnsResolver::~MockDnsResolver() = default;

MockAddressResolver::MockAddressResolver() {
  ON_CALL(*this, name()).WillByDefault(Return("envoy.mock.resolver"));
}

MockAddressResolver::~MockAddressResolver() = default;

MockReadFilterCallbacks::MockReadFilterCallbacks() {
  ON_CALL(*this, connection()).WillByDefault(ReturnRef(connection_));
  ON_CALL(*this, upstreamHost()).WillByDefault(ReturnPointee(&host_));
  ON_CALL(*this, upstreamHost(_)).WillByDefault(SaveArg<0>(&host_));
}

MockReadFilterCallbacks::~MockReadFilterCallbacks() = default;

MockReadFilter::MockReadFilter() {
  ON_CALL(*this, onData(_, _)).WillByDefault(Return(FilterStatus::StopIteration));
  EXPECT_CALL(*this, initializeReadFilterCallbacks(_))
      .WillOnce(
          Invoke([this](ReadFilterCallbacks& callbacks) -> void { callbacks_ = &callbacks; }));
}

MockReadFilter::~MockReadFilter() = default;

MockWriteFilterCallbacks::MockWriteFilterCallbacks() {
  ON_CALL(*this, connection()).WillByDefault(ReturnRef(connection_));
}

MockWriteFilterCallbacks::~MockWriteFilterCallbacks() = default;

MockWriteFilter::MockWriteFilter() {
  EXPECT_CALL(*this, initializeWriteFilterCallbacks(_))
      .WillOnce(Invoke(
          [this](WriteFilterCallbacks& callbacks) -> void { write_callbacks_ = &callbacks; }));
}
MockWriteFilter::~MockWriteFilter() = default;

MockFilter::MockFilter() {
  EXPECT_CALL(*this, initializeReadFilterCallbacks(_))
      .WillOnce(
          Invoke([this](ReadFilterCallbacks& callbacks) -> void { callbacks_ = &callbacks; }));
  EXPECT_CALL(*this, initializeWriteFilterCallbacks(_))
      .WillOnce(Invoke(
          [this](WriteFilterCallbacks& callbacks) -> void { write_callbacks_ = &callbacks; }));
}

MockFilter::~MockFilter() = default;

MockTcpListenerCallbacks::MockTcpListenerCallbacks() = default;
MockTcpListenerCallbacks::~MockTcpListenerCallbacks() = default;

MockUdpListenerCallbacks::MockUdpListenerCallbacks() = default;
MockUdpListenerCallbacks::~MockUdpListenerCallbacks() = default;

MockDrainDecision::MockDrainDecision() = default;
MockDrainDecision::~MockDrainDecision() = default;

MockListenerFilter::~MockListenerFilter() { destroy_(); }

MockListenerFilterCallbacks::MockListenerFilterCallbacks()
    : filter_state_(StreamInfo::FilterStateImpl(StreamInfo::FilterState::LifeSpan::FilterChain)) {
  ON_CALL(*this, filterState()).WillByDefault(ReturnRef(filter_state_));
  ON_CALL(*this, socket()).WillByDefault(ReturnRef(socket_));
}
MockListenerFilterCallbacks::~MockListenerFilterCallbacks() = default;

MockListenerFilterManager::MockListenerFilterManager() = default;
MockListenerFilterManager::~MockListenerFilterManager() = default;

MockFilterChain::MockFilterChain() = default;
MockFilterChain::~MockFilterChain() = default;

MockFilterChainInfo::MockFilterChainInfo() {
  ON_CALL(*this, name()).WillByDefault(Invoke([this]() {
    return absl::string_view{filter_chain_name_};
  }));
}

MockFilterChainManager::MockFilterChainManager() = default;
MockFilterChainManager::~MockFilterChainManager() = default;

MockFilterChainFactory::MockFilterChainFactory() {
  ON_CALL(*this, createListenerFilterChain(_)).WillByDefault(Return(true));
}
MockFilterChainFactory::~MockFilterChainFactory() = default;

MockListenSocket::MockListenSocket()
    : io_handle_(std::make_unique<NiceMock<MockIoHandle>>()),
      connection_info_provider_(std::make_shared<ConnectionInfoSetterImpl>(
          std::make_shared<Address::Ipv4Instance>(80), nullptr)) {
  ON_CALL(*this, options()).WillByDefault(ReturnRef(options_));
  ON_CALL(*this, ioHandle()).WillByDefault(ReturnRef(*io_handle_));
  ON_CALL(testing::Const(*this), ioHandle()).WillByDefault(ReturnRef(*io_handle_));
  ON_CALL(*this, close()).WillByDefault(Invoke([this]() { socket_is_open_ = false; }));
  ON_CALL(testing::Const(*this), isOpen()).WillByDefault(Invoke([this]() {
    return socket_is_open_;
  }));
  ON_CALL(*this, ipVersion())
      .WillByDefault(Return(connection_info_provider_->localAddress()->ip()->version()));
  ON_CALL(*this, duplicate()).WillByDefault(Invoke([]() {
    return std::make_unique<NiceMock<MockListenSocket>>();
  }));
}

MockSocketOption::MockSocketOption() {
  ON_CALL(*this, setOption(_, _)).WillByDefault(Return(true));
}

MockSocketOption::~MockSocketOption() = default;

MockConnectionSocket::MockConnectionSocket()
    : io_handle_(std::make_unique<IoSocketHandleImpl>()),
      connection_info_provider_(
          std::make_shared<ConnectionInfoSetterImpl>(std::make_shared<Address::Ipv4Instance>(80),
                                                     std::make_shared<Address::Ipv4Instance>(80))) {
  ON_CALL(*this, ioHandle()).WillByDefault(ReturnRef(*io_handle_));
  ON_CALL(testing::Const(*this), ioHandle()).WillByDefault(ReturnRef(*io_handle_));
  ON_CALL(*this, ipVersion())
      .WillByDefault(Return(connection_info_provider_->localAddress()->ip()->version()));
}

MockConnectionSocket::~MockConnectionSocket() = default;

MockListener::MockListener() = default;

MockListener::~MockListener() { onDestroy(); }

MockConnectionHandler::MockConnectionHandler() {
  ON_CALL(*this, incNumConnections()).WillByDefault(Invoke([this]() {
    ++num_handler_connections_;
  }));
  ON_CALL(*this, decNumConnections()).WillByDefault(Invoke([this]() {
    --num_handler_connections_;
  }));
  ON_CALL(*this, numConnections()).WillByDefault(Invoke([this]() {
    return num_handler_connections_;
  }));
}
MockConnectionHandler::~MockConnectionHandler() = default;

MockUdpListenerWorkerRouter::~MockUdpListenerWorkerRouter() = default;

MockIp::MockIp() = default;
MockIp::~MockIp() = default;

MockResolvedAddress::MockResolvedAddress(const std::string& logical, const std::string& physical)
    : logical_(logical), physical_(physical) {}
MockResolvedAddress::~MockResolvedAddress() = default;

MockTransportSocketCallbacks::MockTransportSocketCallbacks() {
  ON_CALL(*this, connection()).WillByDefault(ReturnRef(connection_));
}
MockTransportSocketCallbacks::~MockTransportSocketCallbacks() = default;

MockUdpPacketWriter::MockUdpPacketWriter() = default;
MockUdpPacketWriter::~MockUdpPacketWriter() = default;

MockUdpListener::MockUdpListener() {
  ON_CALL(*this, dispatcher()).WillByDefault(ReturnRef(dispatcher_));
}

MockUdpListener::~MockUdpListener() { onDestroy(); }

MockUdpReadFilterCallbacks::MockUdpReadFilterCallbacks() {
  ON_CALL(*this, udpListener()).WillByDefault(ReturnRef(udp_listener_));
}

MockUdpReadFilterCallbacks::~MockUdpReadFilterCallbacks() = default;

MockUdpListenerReadFilter::MockUdpListenerReadFilter(UdpReadFilterCallbacks& callbacks)
    : UdpListenerReadFilter(callbacks) {}
MockUdpListenerReadFilter::~MockUdpListenerReadFilter() = default;

MockUdpListenerFilterManager::MockUdpListenerFilterManager() = default;
MockUdpListenerFilterManager::~MockUdpListenerFilterManager() = default;

MockConnectionBalancer::MockConnectionBalancer() = default;
MockConnectionBalancer::~MockConnectionBalancer() = default;

MockListenerFilterMatcher::MockListenerFilterMatcher() = default;
MockListenerFilterMatcher::~MockListenerFilterMatcher() = default;

} // namespace Network
} // namespace Envoy
