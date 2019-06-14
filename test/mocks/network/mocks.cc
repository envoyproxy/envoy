#include "mocks.h"

#include <cstdint>

#include "envoy/buffer/buffer.h"
#include "envoy/server/listener_manager.h"

#include "common/network/address_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/utility.h"

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

MockListenerConfig::MockListenerConfig() {
  ON_CALL(*this, filterChainFactory()).WillByDefault(ReturnRef(filter_chain_factory_));
  ON_CALL(*this, socket()).WillByDefault(ReturnRef(socket_));
  ON_CALL(*this, listenerScope()).WillByDefault(ReturnRef(scope_));
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
}
MockListenerConfig::~MockListenerConfig() {}

MockActiveDnsQuery::MockActiveDnsQuery() {}
MockActiveDnsQuery::~MockActiveDnsQuery() {}

MockDnsResolver::MockDnsResolver() {
  ON_CALL(*this, resolve(_, _, _)).WillByDefault(Return(&active_query_));
}

MockDnsResolver::~MockDnsResolver() {}

MockAddressResolver::MockAddressResolver() {
  ON_CALL(*this, name()).WillByDefault(Return("envoy.mock.resolver"));
}

MockAddressResolver::~MockAddressResolver() {}

MockReadFilterCallbacks::MockReadFilterCallbacks() {
  ON_CALL(*this, connection()).WillByDefault(ReturnRef(connection_));
  ON_CALL(*this, upstreamHost()).WillByDefault(ReturnPointee(&host_));
  ON_CALL(*this, upstreamHost(_)).WillByDefault(SaveArg<0>(&host_));
}

MockReadFilterCallbacks::~MockReadFilterCallbacks() {}

MockReadFilter::MockReadFilter() {
  ON_CALL(*this, onData(_, _)).WillByDefault(Return(FilterStatus::StopIteration));
  EXPECT_CALL(*this, initializeReadFilterCallbacks(_))
      .WillOnce(
          Invoke([this](ReadFilterCallbacks& callbacks) -> void { callbacks_ = &callbacks; }));
}

MockReadFilter::~MockReadFilter() {}

MockWriteFilterCallbacks::MockWriteFilterCallbacks() {
  ON_CALL(*this, connection()).WillByDefault(ReturnRef(connection_));
}

MockWriteFilterCallbacks::~MockWriteFilterCallbacks() {}

MockWriteFilter::MockWriteFilter() {
  EXPECT_CALL(*this, initializeWriteFilterCallbacks(_))
      .WillOnce(Invoke(
          [this](WriteFilterCallbacks& callbacks) -> void { write_callbacks_ = &callbacks; }));
}
MockWriteFilter::~MockWriteFilter() {}

MockFilter::MockFilter() {
  EXPECT_CALL(*this, initializeReadFilterCallbacks(_))
      .WillOnce(
          Invoke([this](ReadFilterCallbacks& callbacks) -> void { callbacks_ = &callbacks; }));
  EXPECT_CALL(*this, initializeWriteFilterCallbacks(_))
      .WillOnce(Invoke(
          [this](WriteFilterCallbacks& callbacks) -> void { write_callbacks_ = &callbacks; }));
}

MockFilter::~MockFilter() {}

MockListenerCallbacks::MockListenerCallbacks() {}
MockListenerCallbacks::~MockListenerCallbacks() {}

MockUdpListenerCallbacks::MockUdpListenerCallbacks() {}
MockUdpListenerCallbacks::~MockUdpListenerCallbacks() {}

MockDrainDecision::MockDrainDecision() {}
MockDrainDecision::~MockDrainDecision() {}

MockListenerFilter::MockListenerFilter() {}
MockListenerFilter::~MockListenerFilter() {}

MockListenerFilterCallbacks::MockListenerFilterCallbacks() {
  ON_CALL(*this, socket()).WillByDefault(ReturnRef(socket_));
}
MockListenerFilterCallbacks::~MockListenerFilterCallbacks() {}

MockListenerFilterManager::MockListenerFilterManager() {}
MockListenerFilterManager::~MockListenerFilterManager() {}

MockFilterChain::MockFilterChain() {}
MockFilterChain::~MockFilterChain() {}

MockFilterChainManager::MockFilterChainManager() {}
MockFilterChainManager::~MockFilterChainManager() {}

MockFilterChainFactory::MockFilterChainFactory() {
  ON_CALL(*this, createListenerFilterChain(_)).WillByDefault(Return(true));
  ON_CALL(*this, createUdpListenerFilterChain(_, _)).WillByDefault(Return(true));
}
MockFilterChainFactory::~MockFilterChainFactory() {}

MockListenSocket::MockListenSocket()
    : io_handle_(std::make_unique<IoSocketHandleImpl>()),
      local_address_(new Address::Ipv4Instance(80)) {
  ON_CALL(*this, localAddress()).WillByDefault(ReturnRef(local_address_));
  ON_CALL(*this, options()).WillByDefault(ReturnRef(options_));
  ON_CALL(*this, ioHandle()).WillByDefault(ReturnRef(*io_handle_));
  ON_CALL(testing::Const(*this), ioHandle()).WillByDefault(ReturnRef(*io_handle_));
}

MockSocketOption::MockSocketOption() {
  ON_CALL(*this, setOption(_, _)).WillByDefault(Return(true));
}

MockSocketOption::~MockSocketOption() {}

MockConnectionSocket::MockConnectionSocket()
    : io_handle_(std::make_unique<IoSocketHandleImpl>()),
      local_address_(new Address::Ipv4Instance(80)),
      remote_address_(new Address::Ipv4Instance(80)) {
  ON_CALL(*this, localAddress()).WillByDefault(ReturnRef(local_address_));
  ON_CALL(*this, remoteAddress()).WillByDefault(ReturnRef(remote_address_));
  ON_CALL(*this, ioHandle()).WillByDefault(ReturnRef(*io_handle_));
  ON_CALL(testing::Const(*this), ioHandle()).WillByDefault(ReturnRef(*io_handle_));
}

MockListener::MockListener() {}
MockListener::~MockListener() { onDestroy(); }

MockConnectionHandler::MockConnectionHandler() {}
MockConnectionHandler::~MockConnectionHandler() {}

MockIp::MockIp() {}
MockIp::~MockIp() {}

MockResolvedAddress::~MockResolvedAddress() {}

MockTransportSocket::MockTransportSocket() {
  ON_CALL(*this, setTransportSocketCallbacks(_))
      .WillByDefault(Invoke([&](TransportSocketCallbacks& callbacks) { callbacks_ = &callbacks; }));
}
MockTransportSocket::~MockTransportSocket() {}

MockTransportSocketFactory::MockTransportSocketFactory() {}
MockTransportSocketFactory::~MockTransportSocketFactory() {}

MockTransportSocketCallbacks::MockTransportSocketCallbacks() {
  ON_CALL(*this, connection()).WillByDefault(ReturnRef(connection_));
}
MockTransportSocketCallbacks::~MockTransportSocketCallbacks() {}

MockUdpListener::MockUdpListener() {}
MockUdpListener::~MockUdpListener() { onDestroy(); }

MockUdpReadFilterCallbacks::MockUdpReadFilterCallbacks() {
  ON_CALL(*this, udpListener()).WillByDefault(ReturnRef(udp_listener_));
}

MockUdpReadFilterCallbacks::~MockUdpReadFilterCallbacks() {}

MockUdpListenerReadFilter::MockUdpListenerReadFilter(UdpReadFilterCallbacks& callbacks)
    : UdpListenerReadFilter(callbacks) {}
MockUdpListenerReadFilter::~MockUdpListenerReadFilter() {}

MockUdpListenerFilterManager::MockUdpListenerFilterManager() {}
MockUdpListenerFilterManager::~MockUdpListenerFilterManager() {}

} // namespace Network
} // namespace Envoy
