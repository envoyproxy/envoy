#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/stats/stats_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/server/mocks.h"

using testing::_;
using testing::Sequence;
using testing::InSequence;
using testing::Invoke;
using testing::Return;
using testing::StrictMock;
using testing::Test;

namespace Network {

TEST(ConnectionImplUtility, updateBufferStats) {
  StrictMock<Stats::MockCounter> counter;
  StrictMock<Stats::MockGauge> gauge;
  uint64_t previous_total = 0;

  InSequence s;
  EXPECT_CALL(counter, add(5));
  EXPECT_CALL(gauge, add(5));
  ConnectionImplUtility::updateBufferStats(5, 5, previous_total, counter, gauge);
  EXPECT_EQ(5UL, previous_total);

  EXPECT_CALL(counter, add(1));
  EXPECT_CALL(gauge, sub(1));
  ConnectionImplUtility::updateBufferStats(1, 4, previous_total, counter, gauge);

  EXPECT_CALL(gauge, sub(4));
  ConnectionImplUtility::updateBufferStats(0, 0, previous_total, counter, gauge);

  EXPECT_CALL(counter, add(3));
  EXPECT_CALL(gauge, add(3));
  ConnectionImplUtility::updateBufferStats(3, 3, previous_total, counter, gauge);
}

TEST(ConnectionImplDeathTest, BadFd) {
  Event::DispatcherImpl dispatcher;
  EXPECT_DEATH(ConnectionImpl(dispatcher, -1, Utility::resolveUrl("tcp://127.0.0.1:0"),
                              Utility::resolveUrl("tcp://127.0.0.1:0")),
               ".*assert failure: fd_ != -1.*");
}

struct MockBufferStats {
  Connection::BufferStats toBufferStats() {
    return {rx_total_, rx_current_, tx_total_, tx_current_};
  }

  StrictMock<Stats::MockCounter> rx_total_;
  StrictMock<Stats::MockGauge> rx_current_;
  StrictMock<Stats::MockCounter> tx_total_;
  StrictMock<Stats::MockGauge> tx_current_;
};

TEST(ConnectionImplTest, BufferStats) {
  Stats::IsolatedStoreImpl stats_store;
  Event::DispatcherImpl dispatcher;
  Network::TcpListenSocket socket(uint32_t(10000), true);
  Network::MockListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener = dispatcher.createListener(
      connection_handler, socket, listener_callbacks, stats_store, true, false, false, 0);

  Network::ClientConnectionPtr client_connection =
      dispatcher.createClientConnection(Utility::resolveUrl("tcp://127.0.0.1:10000"));
  MockConnectionCallbacks client_callbacks;
  client_connection->addConnectionCallbacks(client_callbacks);
  MockBufferStats client_buffer_stats;
  client_connection->setBufferStats(client_buffer_stats.toBufferStats());
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
  EXPECT_CALL(client_callbacks, onEvent(ConnectionEvent::Connected)).InSequence(s1);
  EXPECT_CALL(client_buffer_stats.tx_total_, add(4)).InSequence(s1);

  Network::ConnectionPtr server_connection;
  Network::MockConnectionCallbacks server_callbacks;
  MockBufferStats server_buffer_stats;
  std::shared_ptr<MockReadFilter> read_filter(new MockReadFilter());
  EXPECT_CALL(listener_callbacks, onNewConnection_(_))
      .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
        server_connection = std::move(conn);
        server_connection->addConnectionCallbacks(server_callbacks);
        server_connection->setBufferStats(server_buffer_stats.toBufferStats());
        server_connection->addReadFilter(read_filter);
        EXPECT_EQ("", server_connection->nextProtocol());
      }));

  Sequence s2;
  EXPECT_CALL(server_buffer_stats.rx_total_, add(4)).InSequence(s2);
  EXPECT_CALL(server_buffer_stats.rx_current_, add(4)).InSequence(s2);
  EXPECT_CALL(server_buffer_stats.rx_current_, sub(4)).InSequence(s2);
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

class ReadBufferLimitTest : public testing::Test {
public:
  void readBufferLimitTest(size_t read_buffer_limit, size_t expected_chunk_size) {
    const size_t buffer_size = 256 * 1024;

    Stats::IsolatedStoreImpl stats_store;
    Event::DispatcherImpl dispatcher;
    Network::TcpListenSocket socket(uint32_t(10000), true);
    Network::MockListenerCallbacks listener_callbacks;
    Network::MockConnectionHandler connection_handler;
    Network::ListenerPtr listener =
        dispatcher.createListener(connection_handler, socket, listener_callbacks, stats_store, true,
                                  false, false, read_buffer_limit);

    Network::ClientConnectionPtr client_connection =
        dispatcher.createClientConnection(Utility::resolveUrl("tcp://127.0.0.1:10000"));
    client_connection->connect();

    Network::ConnectionPtr server_connection;
    std::shared_ptr<MockReadFilter> read_filter(new MockReadFilter());
    EXPECT_CALL(listener_callbacks, onNewConnection_(_))
        .WillOnce(Invoke([&](Network::ConnectionPtr& conn) -> void {
          server_connection = std::move(conn);
          server_connection->addReadFilter(read_filter);
          EXPECT_EQ("", server_connection->nextProtocol());
        }));

    size_t filter_seen = 0;

    EXPECT_CALL(*read_filter, onNewConnection());
    EXPECT_CALL(*read_filter, onData(_))
        .WillRepeatedly(Invoke([&](Buffer::Instance& data) -> FilterStatus {
          EXPECT_EQ(expected_chunk_size, data.length());
          filter_seen += data.length();
          data.drain(data.length());
          if (filter_seen == buffer_size) {
            server_connection->close(ConnectionCloseType::FlushWrite);
          }
          return FilterStatus::StopIteration;
        }));

    MockConnectionCallbacks client_callbacks;
    client_connection->addConnectionCallbacks(client_callbacks);
    EXPECT_CALL(client_callbacks, onEvent(ConnectionEvent::Connected));
    EXPECT_CALL(client_callbacks, onEvent(ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](uint32_t) -> void {
          EXPECT_EQ(buffer_size, filter_seen);
          dispatcher.exit();
        }));

    Buffer::OwnedImpl data(std::string(buffer_size, 'a'));
    client_connection->write(data);
    dispatcher.run(Event::Dispatcher::RunType::Block);
  }
};

TEST_F(ReadBufferLimitTest, NoLimit) { readBufferLimitTest(0, 256 * 1024); }

TEST_F(ReadBufferLimitTest, SomeLimit) { readBufferLimitTest(32 * 1024, 32 * 1024); }

TEST(TcpClientConnectionImplTest, BadConnectNotConnRefused) {
  Event::DispatcherImpl dispatcher;
  // Connecting to 255.255.255.255 will cause a perm error and not ECONNREFUSED which is a
  // different path in libevent. Make sure this doesn't crash.
  ClientConnectionPtr connection =
      dispatcher.createClientConnection(Utility::resolveUrl("tcp://255.255.255.255:1"));
  connection->connect();
  connection->noDelay(true);
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

TEST(TcpClientConnectionImplTest, BadConnectConnRefused) {
  Event::DispatcherImpl dispatcher;
  // Connecting to an invalid port on localhost will cause ECONNREFUSED which is a different code
  // path from other errors. Test this also.
  ClientConnectionPtr connection =
      dispatcher.createClientConnection(Utility::resolveUrl("tcp://127.0.0.1:1"));
  connection->connect();
  connection->noDelay(true);
  dispatcher.run(Event::Dispatcher::RunType::Block);
}

} // Network
