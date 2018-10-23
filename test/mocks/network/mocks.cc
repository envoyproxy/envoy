#include "mocks.h"

#include <cstdint>

#include "envoy/buffer/buffer.h"
#include "envoy/server/listener_manager.h"

#include "common/network/address_impl.h"
#include "common/network/utility.h"

#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Const;
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

MockConnectionCallbacks::MockConnectionCallbacks() {}
MockConnectionCallbacks::~MockConnectionCallbacks() {}

uint64_t MockConnectionBase::next_id_;

void MockConnectionBase::raiseEvent(Network::ConnectionEvent event) {
  if (event == Network::ConnectionEvent::RemoteClose ||
      event == Network::ConnectionEvent::LocalClose) {
    if (state_ == Connection::State::Closed) {
      return;
    }

    state_ = Connection::State::Closed;
  }

  for (Network::ConnectionCallbacks* callbacks : callbacks_) {
    callbacks->onEvent(event);
  }
}

void MockConnectionBase::raiseBytesSentCallbacks(uint64_t num_bytes) {
  for (Network::Connection::BytesSentCb& cb : bytes_sent_callbacks_) {
    cb(num_bytes);
  }
}

void MockConnectionBase::runHighWatermarkCallbacks() {
  for (auto* callback : callbacks_) {
    callback->onAboveWriteBufferHighWatermark();
  }
}

void MockConnectionBase::runLowWatermarkCallbacks() {
  for (auto* callback : callbacks_) {
    callback->onBelowWriteBufferLowWatermark();
  }
}

template <class T> static void initializeMockConnection(T& connection) {
  ON_CALL(connection, dispatcher()).WillByDefault(ReturnRef(connection.dispatcher_));
  ON_CALL(connection, readEnabled()).WillByDefault(ReturnPointee(&connection.read_enabled_));
  ON_CALL(connection, addConnectionCallbacks(_))
      .WillByDefault(Invoke([&connection](Network::ConnectionCallbacks& callbacks) -> void {
        connection.callbacks_.push_back(&callbacks);
      }));
  ON_CALL(connection, addBytesSentCallback(_))
      .WillByDefault(Invoke([&connection](Network::Connection::BytesSentCb cb) {
        connection.bytes_sent_callbacks_.emplace_back(cb);
      }));
  ON_CALL(connection, close(_)).WillByDefault(Invoke([&connection](ConnectionCloseType) -> void {
    connection.raiseEvent(Network::ConnectionEvent::LocalClose);
  }));
  ON_CALL(connection, remoteAddress()).WillByDefault(ReturnRef(connection.remote_address_));
  ON_CALL(connection, localAddress()).WillByDefault(ReturnRef(connection.local_address_));
  ON_CALL(connection, id()).WillByDefault(Return(connection.next_id_));
  ON_CALL(connection, state()).WillByDefault(ReturnPointee(&connection.state_));

  // The real implementation will move the buffer data into the socket.
  ON_CALL(connection, write(_, _)).WillByDefault(Invoke([](Buffer::Instance& buffer, bool) -> void {
    buffer.drain(buffer.length());
  }));

  ON_CALL(connection, streamInfo()).WillByDefault(ReturnRef(connection.stream_info_));
  ON_CALL(Const(connection), streamInfo()).WillByDefault(ReturnRef(connection.stream_info_));
}

MockConnection::MockConnection() {
  remote_address_ = Utility::resolveUrl("tcp://10.0.0.3:50000");
  initializeMockConnection(*this);
}
MockConnection::~MockConnection() {}

MockClientConnection::MockClientConnection() {
  remote_address_ = Utility::resolveUrl("tcp://10.0.0.1:443");
  local_address_ = Utility::resolveUrl("tcp://10.0.0.2:40000");
  initializeMockConnection(*this);
}

MockClientConnection::~MockClientConnection() {}

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

MockWriteFilter::MockWriteFilter() {}
MockWriteFilter::~MockWriteFilter() {}

MockFilter::MockFilter() {
  EXPECT_CALL(*this, initializeReadFilterCallbacks(_))
      .WillOnce(
          Invoke([this](ReadFilterCallbacks& callbacks) -> void { callbacks_ = &callbacks; }));
}

MockFilter::~MockFilter() {}

MockListenerCallbacks::MockListenerCallbacks() {}
MockListenerCallbacks::~MockListenerCallbacks() {}

MockDrainDecision::MockDrainDecision() {}
MockDrainDecision::~MockDrainDecision() {}

MockListenerFilter::MockListenerFilter() {}
MockListenerFilter::~MockListenerFilter() {}

MockListenerFilterCallbacks::MockListenerFilterCallbacks() {}
MockListenerFilterCallbacks::~MockListenerFilterCallbacks() {}

MockListenerFilterManager::MockListenerFilterManager() {}
MockListenerFilterManager::~MockListenerFilterManager() {}

MockFilterChain::MockFilterChain() {}
MockFilterChain::~MockFilterChain() {}

MockFilterChainManager::MockFilterChainManager() {}
MockFilterChainManager::~MockFilterChainManager() {}

MockFilterChainFactory::MockFilterChainFactory() {
  ON_CALL(*this, createListenerFilterChain(_)).WillByDefault(Return(true));
}
MockFilterChainFactory::~MockFilterChainFactory() {}

MockListenSocket::MockListenSocket() : local_address_(new Address::Ipv4Instance(80)) {
  ON_CALL(*this, localAddress()).WillByDefault(ReturnRef(local_address_));
  ON_CALL(*this, options()).WillByDefault(ReturnRef(options_));
  ON_CALL(*this, fd()).WillByDefault(Return(-1));
}

MockListenSocket::~MockListenSocket() {}

MockSocketOption::MockSocketOption() {
  ON_CALL(*this, setOption(_, _)).WillByDefault(Return(true));
}

MockSocketOption::~MockSocketOption() {}

MockConnectionSocket::MockConnectionSocket() : local_address_(new Address::Ipv4Instance(80)) {
  ON_CALL(*this, localAddress()).WillByDefault(ReturnRef(local_address_));
}

MockConnectionSocket::~MockConnectionSocket() {}

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

} // namespace Network
} // namespace Envoy
