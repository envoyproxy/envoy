#include "mocks.h"

#include "envoy/buffer/buffer.h"

#include "common/network/address_impl.h"
#include "common/network/utility.h"

using testing::_;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;
using testing::SaveArg;

namespace Network {

MockConnectionCallbacks::MockConnectionCallbacks() {}
MockConnectionCallbacks::~MockConnectionCallbacks() {}

uint64_t MockConnectionBase::next_id_;

void MockConnectionBase::raiseEvents(uint32_t events) {
  if ((events & Network::ConnectionEvent::RemoteClose) ||
      (events & Network::ConnectionEvent::LocalClose)) {
    if (state_ == Connection::State::Closed) {
      return;
    }

    state_ = Connection::State::Closed;
  }

  for (Network::ConnectionCallbacks* callbacks : callbacks_) {
    callbacks->onEvent(events);
  }
}

template <class T> static void initializeMockConnection(T& connection) {
  ON_CALL(connection, dispatcher()).WillByDefault(ReturnRef(connection.dispatcher_));
  ON_CALL(connection, readEnabled()).WillByDefault(ReturnPointee(&connection.read_enabled_));
  ON_CALL(connection, addConnectionCallbacks(_))
      .WillByDefault(Invoke([&connection](Network::ConnectionCallbacks& callbacks)
                                -> void { connection.callbacks_.push_back(&callbacks); }));
  ON_CALL(connection, close(_))
      .WillByDefault(Invoke([&connection](ConnectionCloseType) -> void {
        connection.raiseEvents(Network::ConnectionEvent::LocalClose);
      }));
  ON_CALL(connection, remoteAddress()).WillByDefault(ReturnPointee(connection.remote_address_));
  ON_CALL(connection, id()).WillByDefault(Return(connection.next_id_));
  ON_CALL(connection, state()).WillByDefault(ReturnPointee(&connection.state_));

  // The real implementation will move the buffer data into the socket.
  ON_CALL(connection, write(_))
      .WillByDefault(
          Invoke([](Buffer::Instance& buffer) -> void { buffer.drain(buffer.length()); }));
}

MockConnection::MockConnection() { initializeMockConnection(*this); }
MockConnection::~MockConnection() {}

MockClientConnection::MockClientConnection() {
  remote_address_ = Utility::resolveUrl("tcp://10.0.0.1:443");
  initializeMockConnection(*this);
}

MockClientConnection::~MockClientConnection() {}

MockActiveDnsQuery::MockActiveDnsQuery() {}
MockActiveDnsQuery::~MockActiveDnsQuery() {}

MockDnsResolver::MockDnsResolver() {
  ON_CALL(*this, resolve(_, _)).WillByDefault(ReturnRef(active_query_));
}

MockDnsResolver::~MockDnsResolver() {}

MockReadFilterCallbacks::MockReadFilterCallbacks() {
  ON_CALL(*this, connection()).WillByDefault(ReturnRef(connection_));
  ON_CALL(*this, upstreamHost()).WillByDefault(ReturnPointee(&host_));
  ON_CALL(*this, upstreamHost(_)).WillByDefault(SaveArg<0>(&host_));
}

MockReadFilterCallbacks::~MockReadFilterCallbacks() {}

MockReadFilter::MockReadFilter() {
  ON_CALL(*this, onData(_)).WillByDefault(Return(FilterStatus::StopIteration));
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

MockFilterChainFactory::MockFilterChainFactory() {}
MockFilterChainFactory::~MockFilterChainFactory() {}

MockListenSocket::MockListenSocket() : local_address_(new Address::Ipv4Instance(80)) {
  ON_CALL(*this, localAddress()).WillByDefault(Return(local_address_));
}

MockListenSocket::~MockListenSocket() {}

MockListener::MockListener() {}
MockListener::~MockListener() {}

MockConnectionHandler::MockConnectionHandler() {}
MockConnectionHandler::~MockConnectionHandler() {}

} // Network
