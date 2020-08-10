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

MockListenerConfig::MockListenerConfig()
    : socket_(std::make_shared<testing::NiceMock<MockListenSocket>>()) {
  ON_CALL(*this, filterChainFactory()).WillByDefault(ReturnRef(filter_chain_factory_));
  ON_CALL(*this, listenSocketFactory()).WillByDefault(ReturnRef(socket_factory_));
  ON_CALL(socket_factory_, localAddress()).WillByDefault(ReturnRef(socket_->localAddress()));
  ON_CALL(socket_factory_, getListenSocket()).WillByDefault(Return(socket_));
  ON_CALL(socket_factory_, sharedSocket())
      .WillByDefault(Return(std::reference_wrapper<Socket>(*socket_)));
  ON_CALL(*this, listenerScope()).WillByDefault(ReturnRef(scope_));
  ON_CALL(*this, name()).WillByDefault(ReturnRef(name_));
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

MockListenerCallbacks::MockListenerCallbacks() = default;
MockListenerCallbacks::~MockListenerCallbacks() = default;

MockUdpListenerCallbacks::MockUdpListenerCallbacks() = default;
MockUdpListenerCallbacks::~MockUdpListenerCallbacks() = default;

MockDrainDecision::MockDrainDecision() = default;
MockDrainDecision::~MockDrainDecision() = default;

MockListenerFilter::MockListenerFilter() = default;
MockListenerFilter::~MockListenerFilter() { destroy_(); }

MockListenerFilterCallbacks::MockListenerFilterCallbacks() {
  ON_CALL(*this, socket()).WillByDefault(ReturnRef(socket_));
}
MockListenerFilterCallbacks::~MockListenerFilterCallbacks() = default;

MockListenerFilterManager::MockListenerFilterManager() = default;
MockListenerFilterManager::~MockListenerFilterManager() = default;

MockFilterChain::MockFilterChain() = default;
MockFilterChain::~MockFilterChain() = default;

MockFilterChainManager::MockFilterChainManager() = default;
MockFilterChainManager::~MockFilterChainManager() = default;

MockFilterChainFactory::MockFilterChainFactory() {
  ON_CALL(*this, createListenerFilterChain(_)).WillByDefault(Return(true));
}
MockFilterChainFactory::~MockFilterChainFactory() = default;

MockListenSocket::MockListenSocket()
    : io_handle_(std::make_unique<IoSocketHandleImpl>()),
      local_address_(new Address::Ipv4Instance(80)) {
  ON_CALL(*this, localAddress()).WillByDefault(ReturnRef(local_address_));
  ON_CALL(*this, options()).WillByDefault(ReturnRef(options_));
  ON_CALL(*this, ioHandle()).WillByDefault(ReturnRef(*io_handle_));
  ON_CALL(testing::Const(*this), ioHandle()).WillByDefault(ReturnRef(*io_handle_));
  ON_CALL(*this, close()).WillByDefault(Invoke([this]() { socket_is_open_ = false; }));
  ON_CALL(testing::Const(*this), isOpen()).WillByDefault(Invoke([this]() {
    return socket_is_open_;
  }));
  ON_CALL(*this, ipVersion()).WillByDefault(Return(local_address_->ip()->version()));
}

MockSocketOption::MockSocketOption() {
  ON_CALL(*this, setOption(_, _)).WillByDefault(Return(true));
}

MockSocketOption::~MockSocketOption() = default;

MockConnectionSocket::MockConnectionSocket()
    : io_handle_(std::make_unique<IoSocketHandleImpl>()),
      local_address_(new Address::Ipv4Instance(80)),
      remote_address_(new Address::Ipv4Instance(80)) {
  ON_CALL(*this, localAddress()).WillByDefault(ReturnRef(local_address_));
  ON_CALL(*this, remoteAddress()).WillByDefault(ReturnRef(remote_address_));
  ON_CALL(*this, directRemoteAddress()).WillByDefault(ReturnRef(remote_address_));
  ON_CALL(*this, ioHandle()).WillByDefault(ReturnRef(*io_handle_));
  ON_CALL(testing::Const(*this), ioHandle()).WillByDefault(ReturnRef(*io_handle_));
  ON_CALL(*this, ipVersion()).WillByDefault(Return(local_address_->ip()->version()));
}

MockConnectionSocket::~MockConnectionSocket() = default;

MockListener::MockListener() = default;

MockListener::~MockListener() { onDestroy(); }

MockConnectionHandler::MockConnectionHandler() = default;
MockConnectionHandler::~MockConnectionHandler() = default;

MockIp::MockIp() = default;
MockIp::~MockIp() = default;

MockResolvedAddress::~MockResolvedAddress() = default;

MockTransportSocketCallbacks::MockTransportSocketCallbacks() {
  ON_CALL(*this, connection()).WillByDefault(ReturnRef(connection_));
}
MockTransportSocketCallbacks::~MockTransportSocketCallbacks() = default;

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
