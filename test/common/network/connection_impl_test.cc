#include "common/buffer/buffer_impl.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/network/mocks.h"

using testing::_;
using testing::Sequence;
using testing::Invoke;
using testing::Return;
using testing::Test;

namespace Network {

TEST(ConnectionImplTest, BadFd) {
  Event::DispatcherImpl dispatcher;
  ConnectionImpl connection(dispatcher, -1, "127.0.0.1");
  MockConnectionCallbacks callbacks;
  connection.addConnectionCallbacks(callbacks);
  EXPECT_CALL(callbacks, onEvent(ConnectionEvent::RemoteClose));
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST(ConnectionImplTest, BufferCallbacks) {
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(10000);
  Network::MockListenerCallbacks listener_callbacks;
  Network::ListenerPtr listener =
      dispatcher.createListener(socket, listener_callbacks, stats_store, false);

  Network::ClientConnectionPtr client_connection =
      dispatcher.createClientConnection("tcp://127.0.0.1:10000");
  MockConnectionCallbacks client_callbacks;
  client_connection->addConnectionCallbacks(client_callbacks);
  client_connection->connect();

  std::shared_ptr<MockWriteFilter> write_filter(new MockWriteFilter());
  std::shared_ptr<MockFilter> filter(new MockFilter());
  client_connection->addWriteFilter(write_filter);
  client_connection->addFilter(filter);

  Sequence s1;
  EXPECT_CALL(*write_filter, onWrite(_))
      .InSequence(s1)
      .WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_CALL(*write_filter, onWrite(_)).InSequence(s1).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onWrite(_)).InSequence(s1).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(client_callbacks, onBufferChange(ConnectionBufferType::Write, 0, 4)).InSequence(s1);
  EXPECT_CALL(client_callbacks, onEvent(ConnectionEvent::Connected)).InSequence(s1);
  EXPECT_CALL(client_callbacks, onBufferChange(ConnectionBufferType::Write, 4, -4)).InSequence(s1);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_callbacks;
  std::shared_ptr<MockReadFilter> read_filter(new MockReadFilter());
  EXPECT_CALL(listener_callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_callbacks);
        server_connection->addReadFilter(read_filter);
        EXPECT_EQ("", server_connection->nextProtocol());
      }));

  Sequence s2;
  EXPECT_CALL(server_callbacks, onBufferChange(ConnectionBufferType::Read, 0, 4)).InSequence(s2);
  EXPECT_CALL(server_callbacks, onBufferChange(ConnectionBufferType::Read, 4, -4)).InSequence(s2);
  EXPECT_CALL(server_callbacks, onEvent(ConnectionEvent::LocalClose)).InSequence(s2);

  EXPECT_CALL(*read_filter, onNewConnection());
  EXPECT_CALL(*read_filter, onData(_))
      .WillOnce(Invoke([&](Buffer::Instance& data) -> FilterStatus {
        data.drain(data.length());
        server_connection->close(ConnectionCloseType::FlushWrite);
        return FilterStatus::StopIteration;
      }));

  EXPECT_CALL(client_callbacks, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](uint32_t) -> void { dispatcher.exit(); }));

  Buffer::OwnedImpl data("1234");
  client_connection->write(data);
  client_connection->write(data);
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST(TcpClientConnectionImplTest, BadConnectNotConnRefused) {
  Event::DispatcherImpl dispatcher;
  // Connecting to 255.255.255.255 will cause a perm error and not ECONNREFUSED which is a
  // different path in libevent. Make sure this doesn't crash.
  ClientConnectionPtr connection = dispatcher.createClientConnection("tcp://255.255.255.255:1");
  connection->connect();
  connection->noDelay(true);
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST(TcpClientConnectionImplTest, BadConnectConnRefused) {
  Event::DispatcherImpl dispatcher;
  // Connecting to an invalid port on localhost will cause ECONNREFUSED which is a different code
  // path from other errors. Test this also.
  ClientConnectionPtr connection = dispatcher.createClientConnection("tcp://255.255.255.255:1");
  connection->connect();
  connection->noDelay(true);
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

} // Network
