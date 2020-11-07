#include "test/mocks/network/connection.h"

using testing::Const;
using testing::Invoke;
using testing::Return;
using testing::ReturnPointee;
using testing::ReturnRef;

namespace Envoy {
namespace Network {

MockConnectionCallbacks::MockConnectionCallbacks() = default;
MockConnectionCallbacks::~MockConnectionCallbacks() = default;

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
  ON_CALL(connection, directRemoteAddress()).WillByDefault(ReturnRef(connection.remote_address_));
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
MockConnection::~MockConnection() = default;

MockServerConnection::MockServerConnection() {
  remote_address_ = Utility::resolveUrl("tcp://10.0.0.1:443");
  local_address_ = Utility::resolveUrl("tcp://10.0.0.2:40000");
  initializeMockConnection(*this);
}

MockServerConnection::~MockServerConnection() = default;

MockClientConnection::MockClientConnection() {
  remote_address_ = Utility::resolveUrl("tcp://10.0.0.1:443");
  local_address_ = Utility::resolveUrl("tcp://10.0.0.2:40000");
  initializeMockConnection(*this);
}

MockClientConnection::~MockClientConnection() = default;

MockFilterManagerConnection::MockFilterManagerConnection() {
  remote_address_ = Utility::resolveUrl("tcp://10.0.0.3:50000");
  initializeMockConnection(*this);

  // The real implementation will move the buffer data into the socket.
  ON_CALL(*this, rawWrite(_, _)).WillByDefault(Invoke([](Buffer::Instance& buffer, bool) -> void {
    buffer.drain(buffer.length());
  }));
}
MockFilterManagerConnection::~MockFilterManagerConnection() = default;

} // namespace Network
} // namespace Envoy
