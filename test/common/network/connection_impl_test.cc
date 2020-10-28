#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"

#include "common/api/os_sys_calls_impl.h"
#include "common/buffer/buffer_impl.h"
#include "common/common/empty_string.h"
#include "common/common/fmt.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/address_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/listen_socket_impl.h"
#include "common/network/utility.h"
#include "common/runtime/runtime_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Eq;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Optional;
using testing::Return;
using testing::SaveArg;
using testing::Sequence;
using testing::StrictMock;

namespace Envoy {
namespace Network {
namespace {

TEST(RawBufferSocket, TestBasics) {
  TransportSocketPtr raw_buffer_socket(Network::Test::createRawBufferSocket());
  EXPECT_FALSE(raw_buffer_socket->ssl());
  EXPECT_TRUE(raw_buffer_socket->canFlushClose());
  EXPECT_EQ("", raw_buffer_socket->protocol());
}

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

TEST(ConnectionImplBaseUtility, addIdToHashKey) {
  uint64_t connection_id = 0x0123456789abcdef;
  std::vector<uint8_t> hash{{0xff, 0xfe, 0xfd, 0xfc}};
  ConnectionImplBase::addIdToHashKey(hash, connection_id);
  ASSERT_EQ(12, hash.size());
  EXPECT_EQ(0xff, hash[0]);
  EXPECT_EQ(0xfe, hash[1]);
  EXPECT_EQ(0xfd, hash[2]);
  EXPECT_EQ(0xfc, hash[3]);
  EXPECT_EQ(0xef, hash[4]);
  EXPECT_EQ(0xcd, hash[5]);
  EXPECT_EQ(0xab, hash[6]);
  EXPECT_EQ(0x89, hash[7]);
  EXPECT_EQ(0x67, hash[8]);
  EXPECT_EQ(0x45, hash[9]);
  EXPECT_EQ(0x23, hash[10]);
  EXPECT_EQ(0x01, hash[11]);
}

class ConnectionImplDeathTest : public testing::TestWithParam<Address::IpVersion> {};
INSTANTIATE_TEST_SUITE_P(IpVersions, ConnectionImplDeathTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ConnectionImplDeathTest, BadFd) {
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>();
  StreamInfo::StreamInfoImpl stream_info(dispatcher->timeSource());
  EXPECT_DEATH(
      ConnectionImpl(*dispatcher,
                     std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
                     Network::Test::createRawBufferSocket(), stream_info, false),
      ".*assert failure: SOCKET_VALID\\(fd\\)");
}

class TestClientConnectionImpl : public Network::ClientConnectionImpl {
public:
  using ClientConnectionImpl::ClientConnectionImpl;
  Buffer::WatermarkBuffer& readBuffer() { return read_buffer_; }
};

class ConnectionImplTest : public testing::TestWithParam<Address::IpVersion> {
protected:
  ConnectionImplTest() : api_(Api::createApiForTest(time_system_)), stream_info_(time_system_) {}

  void setUpBasicConnection() {
    if (dispatcher_ == nullptr) {
      dispatcher_ = api_->allocateDispatcher("test_thread");
    }
    socket_ = std::make_shared<Network::TcpListenSocket>(
        Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr, true);
    listener_ =
        dispatcher_->createListener(socket_, listener_callbacks_, true, ENVOY_TCP_BACKLOG_SIZE);
    client_connection_ = std::make_unique<Network::TestClientConnectionImpl>(
        *dispatcher_, socket_->localAddress(), source_address_,
        Network::Test::createRawBufferSocket(), socket_options_);
    client_connection_->addConnectionCallbacks(client_callbacks_);
    EXPECT_EQ(nullptr, client_connection_->ssl());
    const Network::ClientConnection& const_connection = *client_connection_;
    EXPECT_EQ(nullptr, const_connection.ssl());
    EXPECT_FALSE(client_connection_->localAddressRestored());
  }

  void connect() {
    int expected_callbacks = 2;
    client_connection_->connect();
    read_filter_ = std::make_shared<NiceMock<MockReadFilter>>();
    EXPECT_CALL(listener_callbacks_, onAccept_(_))
        .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
          server_connection_ = dispatcher_->createServerConnection(
              std::move(socket), Network::Test::createRawBufferSocket(), stream_info_);
          server_connection_->addConnectionCallbacks(server_callbacks_);
          server_connection_->addReadFilter(read_filter_);

          expected_callbacks--;
          if (expected_callbacks == 0) {
            dispatcher_->exit();
          }
        }));
    EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
          expected_callbacks--;
          if (expected_callbacks == 0) {
            dispatcher_->exit();
          }
        }));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  void disconnect(bool wait_for_remote_close) {
    if (client_write_buffer_) {
      EXPECT_CALL(*client_write_buffer_, drain(_))
          .Times(AnyNumber())
          .WillRepeatedly(
              Invoke([&](uint64_t size) -> void { client_write_buffer_->baseDrain(size); }));
    }
    EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
    client_connection_->close(ConnectionCloseType::NoFlush);
    if (wait_for_remote_close) {
      EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
          .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
      dispatcher_->run(Event::Dispatcher::RunType::Block);
    } else {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  void useMockBuffer() {
    // This needs to be called before the dispatcher is created.
    ASSERT(dispatcher_.get() == nullptr);

    MockBufferFactory* factory = new StrictMock<MockBufferFactory>;
    dispatcher_ = api_->allocateDispatcher("test_thread", Buffer::WatermarkFactoryPtr{factory});
    // The first call to create a client session will get a MockBuffer.
    // Other calls for server sessions will by default get a normal OwnedImpl.
    EXPECT_CALL(*factory, create_(_, _, _))
        .Times(AnyNumber())
        .WillOnce(Invoke([&](std::function<void()> below_low, std::function<void()> above_high,
                             std::function<void()> above_overflow) -> Buffer::Instance* {
          client_write_buffer_ = new MockWatermarkBuffer(below_low, above_high, above_overflow);
          return client_write_buffer_;
        }))
        .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                  std::function<void()> above_overflow) -> Buffer::Instance* {
          return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
        }));
  }

protected:
  struct ConnectionMocks {
    std::unique_ptr<NiceMock<Event::MockDispatcher>> dispatcher_;
    Event::MockTimer* timer_;
    std::unique_ptr<NiceMock<MockTransportSocket>> transport_socket_;
    NiceMock<Event::MockFileEvent>* file_event_;
    Event::FileReadyCb* file_ready_cb_;
  };

  ConnectionMocks createConnectionMocks(bool create_timer = true) {
    auto dispatcher = std::make_unique<NiceMock<Event::MockDispatcher>>();
    EXPECT_CALL(dispatcher->buffer_factory_, create_(_, _, _))
        .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                  std::function<void()> above_overflow) -> Buffer::Instance* {
          // ConnectionImpl calls Envoy::MockBufferFactory::create(), which calls create_() and
          // wraps the returned raw pointer below with a unique_ptr.
          return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
        }));

    Event::MockTimer* timer = nullptr;
    if (create_timer) {
      // This timer will be returned (transferring ownership) to the ConnectionImpl when
      // createTimer() is called to allocate the delayed close timer.
      timer = new Event::MockTimer(dispatcher.get());
    }

    NiceMock<Event::MockFileEvent>* file_event = new NiceMock<Event::MockFileEvent>;
    EXPECT_CALL(*dispatcher, createFileEvent_(0, _, _, _))
        .WillOnce(DoAll(SaveArg<1>(&file_ready_cb_), Return(file_event)));

    auto transport_socket = std::make_unique<NiceMock<MockTransportSocket>>();
    EXPECT_CALL(*transport_socket, canFlushClose()).WillRepeatedly(Return(true));

    return ConnectionMocks{std::move(dispatcher), timer, std::move(transport_socket), file_event,
                           &file_ready_cb_};
  }
  Network::TestClientConnectionImpl* testClientConnection() {
    return dynamic_cast<Network::TestClientConnectionImpl*>(client_connection_.get());
  }

  Event::FileReadyCb file_ready_cb_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<Network::TcpListenSocket> socket_{nullptr};
  Network::MockTcpListenerCallbacks listener_callbacks_;
  Network::MockConnectionHandler connection_handler_;
  Network::ListenerPtr listener_;
  Network::ClientConnectionPtr client_connection_;
  StrictMock<MockConnectionCallbacks> client_callbacks_;
  Network::ServerConnectionPtr server_connection_;
  StrictMock<Network::MockConnectionCallbacks> server_callbacks_;
  std::shared_ptr<MockReadFilter> read_filter_;
  MockWatermarkBuffer* client_write_buffer_ = nullptr;
  Address::InstanceConstSharedPtr source_address_;
  Socket::OptionsSharedPtr socket_options_;
  StreamInfo::StreamInfoImpl stream_info_;
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ConnectionImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ConnectionImplTest, UniqueId) {
  setUpBasicConnection();
  disconnect(false);
  uint64_t first_id = client_connection_->id();
  setUpBasicConnection();
  EXPECT_NE(first_id, client_connection_->id());
  disconnect(false);
}

TEST_P(ConnectionImplTest, CloseDuringConnectCallback) {
  setUpBasicConnection();

  Buffer::OwnedImpl buffer("hello world");
  client_connection_->write(buffer, false);
  client_connection_->connect();

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        client_connection_->close(ConnectionCloseType::NoFlush);
      }));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));

  read_filter_ = std::make_shared<NiceMock<MockReadFilter>>();

  EXPECT_CALL(listener_callbacks_, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection_ = dispatcher_->createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket(), stream_info_);
        server_connection_->addConnectionCallbacks(server_callbacks_);
        server_connection_->addReadFilter(read_filter_);
      }));

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(ConnectionImplTest, ImmediateConnectError) {
  dispatcher_ = api_->allocateDispatcher("test_thread");

  // Using a broadcast/multicast address as the connection destinations address causes an
  // immediate error return from connect().
  Address::InstanceConstSharedPtr broadcast_address;
  socket_ = std::make_shared<Network::TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr, true);
  if (socket_->localAddress()->ip()->version() == Address::IpVersion::v4) {
    broadcast_address = std::make_shared<Address::Ipv4Instance>("224.0.0.1", 0);
  } else {
    broadcast_address = std::make_shared<Address::Ipv6Instance>("ff02::1", 0);
  }

  client_connection_ = dispatcher_->createClientConnection(
      broadcast_address, source_address_, Network::Test::createRawBufferSocket(), nullptr);
  client_connection_->addConnectionCallbacks(client_callbacks_);
  client_connection_->connect();

  // Verify that also the immediate connect errors generate a remote close event.
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(ConnectionImplTest, SetServerTransportSocketTimeout) {
  ConnectionMocks mocks = createConnectionMocks(false);
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(0);

  auto* mock_timer = new NiceMock<Event::MockTimer>(mocks.dispatcher_.get());
  auto server_connection = std::make_unique<Network::ServerConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_, true);

  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(3 * 1000), _));
  server_connection->setTransportSocketConnectTimeout(std::chrono::seconds(3));
  EXPECT_CALL(*transport_socket, closeSocket(ConnectionEvent::LocalClose));
  mock_timer->invokeCallback();
  EXPECT_THAT(stream_info_.connectionTerminationDetails(),
              Optional(HasSubstr("transport socket timeout")));
}

TEST_P(ConnectionImplTest, SetServerTransportSocketTimeoutAfterConnect) {
  ConnectionMocks mocks = createConnectionMocks(false);
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(0);

  auto server_connection = std::make_unique<Network::ServerConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_, true);

  transport_socket->callbacks_->raiseEvent(ConnectionEvent::Connected);
  // This should be a no-op. No timer should be created.
  EXPECT_CALL(*mocks.dispatcher_, createTimer_(_)).Times(0);
  server_connection->setTransportSocketConnectTimeout(std::chrono::seconds(3));

  server_connection->close(ConnectionCloseType::NoFlush);
}

TEST_P(ConnectionImplTest, ServerTransportSocketTimeoutDisabledOnConnect) {
  ConnectionMocks mocks = createConnectionMocks(false);
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(0);

  auto* mock_timer = new NiceMock<Event::MockTimer>(mocks.dispatcher_.get());
  auto server_connection = std::make_unique<Network::ServerConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_, true);

  bool timer_destroyed = false;
  mock_timer->timer_destroyed_ = &timer_destroyed;
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(3 * 1000), _));

  server_connection->setTransportSocketConnectTimeout(std::chrono::seconds(3));

  transport_socket->callbacks_->raiseEvent(ConnectionEvent::Connected);
  EXPECT_TRUE(timer_destroyed);

  server_connection->close(ConnectionCloseType::NoFlush);
}

TEST_P(ConnectionImplTest, SocketOptions) {
  Network::ClientConnectionPtr upstream_connection_;

  setUpBasicConnection();

  Buffer::OwnedImpl buffer("hello world");
  client_connection_->write(buffer, false);
  client_connection_->connect();

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        client_connection_->close(ConnectionCloseType::NoFlush);
      }));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));

  read_filter_ = std::make_shared<NiceMock<MockReadFilter>>();

  auto option = std::make_shared<MockSocketOption>();

  EXPECT_CALL(*option, setOption(_, envoy::config::core::v3::SocketOption::STATE_PREBIND))
      .WillOnce(Return(true));
  EXPECT_CALL(listener_callbacks_, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        socket->addOption(option);
        server_connection_ = dispatcher_->createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket(), stream_info_);
        server_connection_->addConnectionCallbacks(server_callbacks_);
        server_connection_->addReadFilter(read_filter_);

        upstream_connection_ = dispatcher_->createClientConnection(
            socket_->localAddress(), source_address_, Network::Test::createRawBufferSocket(),
            server_connection_->socketOptions());
      }));

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        upstream_connection_->close(ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Assert that upstream connection gets the socket options
  ASSERT(upstream_connection_ != nullptr);
  ASSERT(upstream_connection_->socketOptions() != nullptr);
  ASSERT(upstream_connection_->socketOptions()->front() == option);
}

TEST_P(ConnectionImplTest, SocketOptionsFailureTest) {
  Network::ClientConnectionPtr upstream_connection_;
  StrictMock<Network::MockConnectionCallbacks> upstream_callbacks_;

  setUpBasicConnection();

  Buffer::OwnedImpl buffer("hello world");
  client_connection_->write(buffer, false);
  client_connection_->connect();

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        client_connection_->close(ConnectionCloseType::NoFlush);
      }));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));

  read_filter_ = std::make_shared<NiceMock<MockReadFilter>>();

  auto option = std::make_shared<MockSocketOption>();

  EXPECT_CALL(*option, setOption(_, envoy::config::core::v3::SocketOption::STATE_PREBIND))
      .WillOnce(Return(false));
  EXPECT_CALL(listener_callbacks_, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        socket->addOption(option);
        server_connection_ = dispatcher_->createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket(), stream_info_);
        server_connection_->addConnectionCallbacks(server_callbacks_);
        server_connection_->addReadFilter(read_filter_);

        upstream_connection_ = dispatcher_->createClientConnection(
            socket_->localAddress(), source_address_, Network::Test::createRawBufferSocket(),
            server_connection_->socketOptions());
        upstream_connection_->addConnectionCallbacks(upstream_callbacks_);
      }));

  EXPECT_CALL(upstream_callbacks_, onEvent(ConnectionEvent::LocalClose));

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
        upstream_connection_->close(ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

struct MockConnectionStats {
  Connection::ConnectionStats toBufferStats() {
    return {rx_total_,   rx_current_,   tx_total_,
            tx_current_, &bind_errors_, &delayed_close_timeouts_};
  }

  StrictMock<Stats::MockCounter> rx_total_;
  StrictMock<Stats::MockGauge> rx_current_;
  StrictMock<Stats::MockCounter> tx_total_;
  StrictMock<Stats::MockGauge> tx_current_;
  StrictMock<Stats::MockCounter> bind_errors_;
  StrictMock<Stats::MockCounter> delayed_close_timeouts_;
};

struct NiceMockConnectionStats {
  Connection::ConnectionStats toBufferStats() {
    return {rx_total_,   rx_current_,   tx_total_,
            tx_current_, &bind_errors_, &delayed_close_timeouts_};
  }

  NiceMock<Stats::MockCounter> rx_total_;
  NiceMock<Stats::MockGauge> rx_current_;
  NiceMock<Stats::MockCounter> tx_total_;
  NiceMock<Stats::MockGauge> tx_current_;
  NiceMock<Stats::MockCounter> bind_errors_;
  NiceMock<Stats::MockCounter> delayed_close_timeouts_;
};

TEST_P(ConnectionImplTest, ConnectionHash) {
  setUpBasicConnection();

  MockConnectionStats client_connection_stats;
  client_connection_->setConnectionStats(client_connection_stats.toBufferStats());

  std::vector<uint8_t> hash1;
  std::vector<uint8_t> hash2;
  ConnectionImplBase::addIdToHashKey(hash1, client_connection_->id());
  client_connection_->hashKey(hash2);
  ASSERT_EQ(hash1, hash2);
  disconnect(false);
}

TEST_P(ConnectionImplTest, ConnectionStats) {
  setUpBasicConnection();

  MockConnectionStats client_connection_stats;
  client_connection_->setConnectionStats(client_connection_stats.toBufferStats());
  client_connection_->connect();

  std::shared_ptr<MockWriteFilter> write_filter(new MockWriteFilter());
  std::shared_ptr<MockFilter> filter(new MockFilter());
  client_connection_->addFilter(filter);
  client_connection_->addWriteFilter(write_filter);

  Sequence s1;
  EXPECT_CALL(*write_filter, onWrite(_, _))
      .InSequence(s1)
      .WillOnce(Return(FilterStatus::StopIteration));
  EXPECT_CALL(*write_filter, onWrite(_, _)).InSequence(s1).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*filter, onWrite(_, _)).InSequence(s1).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected)).InSequence(s1);
  EXPECT_CALL(client_connection_stats.tx_total_, add(4)).InSequence(s1);

  read_filter_ = std::make_shared<NiceMock<MockReadFilter>>();
  MockConnectionStats server_connection_stats;
  EXPECT_CALL(listener_callbacks_, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection_ = dispatcher_->createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket(), stream_info_);
        server_connection_->addConnectionCallbacks(server_callbacks_);
        server_connection_->setConnectionStats(server_connection_stats.toBufferStats());
        server_connection_->addReadFilter(read_filter_);
        EXPECT_EQ("", server_connection_->nextProtocol());
      }));

  Sequence s2;
  EXPECT_CALL(server_connection_stats.rx_total_, add(4)).InSequence(s2);
  EXPECT_CALL(server_connection_stats.rx_current_, add(4)).InSequence(s2);
  EXPECT_CALL(server_connection_stats.rx_current_, sub(4)).InSequence(s2);
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose)).InSequence(s2);

  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) -> FilterStatus {
        data.drain(data.length());
        server_connection_->close(ConnectionCloseType::FlushWrite);
        return FilterStatus::StopIteration;
      }));

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

  Buffer::OwnedImpl data("1234");
  client_connection_->write(data, false);
  client_connection_->write(data, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Ensure the new counter logic in ReadDisable avoids tripping asserts in ReadDisable guarding
// against actual enabling twice in a row.
TEST_P(ConnectionImplTest, ReadDisable) {
  ConnectionMocks mocks = createConnectionMocks(false);
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(0);
  auto connection = std::make_unique<Network::ConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_, true);

  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  connection->readDisable(true);
  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  connection->readDisable(false);

  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  connection->readDisable(true);
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  connection->readDisable(true);
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  connection->readDisable(false);
  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  connection->readDisable(false);

  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  connection->readDisable(true);
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  connection->readDisable(true);
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  connection->readDisable(false);
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  connection->readDisable(true);
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  connection->readDisable(false);
  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  connection->readDisable(false);

  connection->close(ConnectionCloseType::NoFlush);
}

// The HTTP/1 codec handles pipelined connections by relying on readDisable(false) resulting in the
// subsequent request being dispatched. Regression test this behavior.
TEST_P(ConnectionImplTest, ReadEnableDispatches) {
  setUpBasicConnection();
  connect();

  std::shared_ptr<MockReadFilter> client_read_filter(new NiceMock<MockReadFilter>());
  client_connection_->addReadFilter(client_read_filter);

  {
    Buffer::OwnedImpl buffer("data");
    server_connection_->write(buffer, false);
    EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("data"), false))
        .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
          dispatcher_->exit();
          return FilterStatus::StopIteration;
        }));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  {
    client_connection_->readDisable(true);
    EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("data"), false))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> FilterStatus {
          buffer.drain(buffer.length());
          dispatcher_->exit();
          return FilterStatus::StopIteration;
        }));
    client_connection_->readDisable(false);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  disconnect(true);
}

// Make sure if we readDisable(true) and schedule a 'kick' and then
// readDisable(false) the kick doesn't happen.
TEST_P(ConnectionImplTest, KickUndone) {
  setUpBasicConnection();
  connect();

  std::shared_ptr<MockReadFilter> client_read_filter(new NiceMock<MockReadFilter>());
  client_connection_->addReadFilter(client_read_filter);
  Buffer::Instance* connection_buffer = nullptr;

  {
    Buffer::OwnedImpl buffer("data");
    server_connection_->write(buffer, false);
    EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("data"), false))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> FilterStatus {
          dispatcher_->exit();
          connection_buffer = &buffer;
          return FilterStatus::StopIteration;
        }));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  {
    // Like ReadEnableDispatches above, read disable and read enable to kick off
    // an extra read. But then readDisable again and make sure the kick doesn't
    // happen.
    client_connection_->readDisable(true);
    client_connection_->readDisable(false); // Sets dispatch_buffered_data_
    client_connection_->readDisable(true);
    EXPECT_CALL(*client_read_filter, onData(_, _)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Now drain the connection's buffer and try to do a read which should _not_
  // pass up the stack (no data is read)
  {
    connection_buffer->drain(connection_buffer->length());
    client_connection_->readDisable(false);
    EXPECT_CALL(*client_read_filter, onData(_, _)).Times(0);
    // Data no longer buffered - even if dispatch_buffered_data_ lingered it should have no effect.
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  disconnect(true);
}

// Ensure that calls to readDisable on a closed connection are handled gracefully. Known past issues
// include a crash on https://github.com/envoyproxy/envoy/issues/3639, and ASSERT failure followed
// by infinite loop in https://github.com/envoyproxy/envoy/issues/9508
TEST_P(ConnectionImplTest, ReadDisableAfterCloseHandledGracefully) {
  setUpBasicConnection();

  client_connection_->readDisable(true);
  client_connection_->readDisable(false);

  client_connection_->readDisable(true);
  client_connection_->readDisable(true);
  client_connection_->readDisable(false);
  client_connection_->readDisable(false);

  client_connection_->readDisable(true);
  client_connection_->readDisable(true);
  disconnect(false);
#ifndef NDEBUG
  // When running in debug mode, verify that calls to readDisable and readEnabled on a closed socket
  // trigger ASSERT failures.
  EXPECT_DEBUG_DEATH(client_connection_->readEnabled(), "");
  EXPECT_DEBUG_DEATH(client_connection_->readDisable(true), "");
  EXPECT_DEBUG_DEATH(client_connection_->readDisable(false), "");
#else
  // When running in release mode, verify that calls to readDisable change the readEnabled state.
  client_connection_->readDisable(false);
  client_connection_->readDisable(true);
  client_connection_->readDisable(false);
  EXPECT_FALSE(client_connection_->readEnabled());
  client_connection_->readDisable(false);
  EXPECT_TRUE(client_connection_->readEnabled());
#endif
}

// On our current macOS build, the client connection does not get the early
// close notification and instead gets the close after reading the FIN.
// The Windows backend in libevent does not support the EV_CLOSED flag
// so it won't detect the early close
#if !defined(__APPLE__) && !defined(WIN32)
TEST_P(ConnectionImplTest, EarlyCloseOnReadDisabledConnection) {
  setUpBasicConnection();
  connect();

  client_connection_->readDisable(true);

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose));
  server_connection_->close(ConnectionCloseType::FlushWrite);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}
#endif

TEST_P(ConnectionImplTest, CloseOnReadDisableWithoutCloseDetection) {
  setUpBasicConnection();
  connect();

  client_connection_->detectEarlyCloseWhenReadDisabled(false);
  client_connection_->readDisable(true);

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose)).Times(0);
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  server_connection_->close(ConnectionCloseType::FlushWrite);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  client_connection_->readDisable(false);
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that connection half-close is sent and received properly.
TEST_P(ConnectionImplTest, HalfClose) {
  setUpBasicConnection();
  connect();

  std::shared_ptr<MockReadFilter> client_read_filter(new NiceMock<MockReadFilter>());
  server_connection_->enableHalfClose(true);
  client_connection_->enableHalfClose(true);
  client_connection_->addReadFilter(client_read_filter);

  EXPECT_CALL(*read_filter_, onData(_, true)).WillOnce(InvokeWithoutArgs([&]() -> FilterStatus {
    dispatcher_->exit();
    return FilterStatus::StopIteration;
  }));

  Buffer::OwnedImpl empty_buffer;
  client_connection_->write(empty_buffer, true);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  Buffer::OwnedImpl buffer("data");
  server_connection_->write(buffer, false);
  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("data"), false))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> FilterStatus {
        buffer.drain(buffer.length());
        dispatcher_->exit();
        return FilterStatus::StopIteration;
      }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose));
  server_connection_->write(empty_buffer, true);
  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual(""), true))
      .WillOnce(InvokeWithoutArgs([&]() -> FilterStatus {
        dispatcher_->exit();
        return FilterStatus::StopIteration;
      }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that connections do not detect early close when half-close is enabled
TEST_P(ConnectionImplTest, HalfCloseNoEarlyCloseDetection) {
  setUpBasicConnection();
  connect();

  server_connection_->enableHalfClose(true);
  server_connection_->readDisable(true);

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose)).Times(0);
  EXPECT_CALL(*read_filter_, onData(_, _)).Times(0);
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  client_connection_->close(ConnectionCloseType::FlushWrite);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  server_connection_->readDisable(false);
  EXPECT_CALL(*read_filter_, onData(_, _)).WillOnce(InvokeWithoutArgs([&]() -> FilterStatus {
    dispatcher_->exit();
    return FilterStatus::StopIteration;
  }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose));
  server_connection_->close(ConnectionCloseType::NoFlush);
}

// Test that as watermark levels are changed, the appropriate callbacks are triggered.
TEST_P(ConnectionImplTest, WriteWatermarks) {
  useMockBuffer();

  setUpBasicConnection();
  EXPECT_FALSE(client_connection_->aboveHighWatermark());

  // Stick 5 bytes in the connection buffer.
  std::unique_ptr<Buffer::OwnedImpl> buffer(new Buffer::OwnedImpl("hello"));
  int buffer_len = buffer->length();
  EXPECT_CALL(*client_write_buffer_, move(_));
  client_write_buffer_->move(*buffer);

  {
    // Go from watermarks being off to being above the high watermark.
    EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark());
    EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
    client_connection_->setBufferLimits(buffer_len - 3);
    EXPECT_TRUE(client_connection_->aboveHighWatermark());
  }

  {
    // Go from above the high watermark to in between both.
    EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark()).Times(0);
    EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
    client_connection_->setBufferLimits(buffer_len + 1);
    EXPECT_TRUE(client_connection_->aboveHighWatermark());
  }

  {
    // Go from above the high watermark to below the low watermark.
    EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark()).Times(0);
    EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark());
    client_connection_->setBufferLimits(buffer_len * 3);
    EXPECT_FALSE(client_connection_->aboveHighWatermark());
  }

  {
    // Go back in between and verify neither callback is called.
    EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark()).Times(0);
    EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
    client_connection_->setBufferLimits(buffer_len * 2);
    EXPECT_FALSE(client_connection_->aboveHighWatermark());
  }

  disconnect(false);
}

// Test that as watermark levels are changed, the appropriate callbacks are triggered.
TEST_P(ConnectionImplTest, ReadWatermarks) {

  setUpBasicConnection();
  client_connection_->setBufferLimits(2);
  std::shared_ptr<MockReadFilter> client_read_filter(new NiceMock<MockReadFilter>());
  client_connection_->addReadFilter(client_read_filter);
  connect();

  auto on_filter_data_exit = [&](Buffer::Instance&, bool) -> FilterStatus {
    dispatcher_->exit();
    return FilterStatus::StopIteration;
  };

  EXPECT_FALSE(testClientConnection()->readBuffer().highWatermarkTriggered());
  EXPECT_TRUE(client_connection_->readEnabled());
  // Add 4 bytes to the buffer and verify the connection becomes read disabled.
  {
    Buffer::OwnedImpl buffer("data");
    server_connection_->write(buffer, false);
    EXPECT_CALL(*client_read_filter, onData(_, false)).WillOnce(Invoke(on_filter_data_exit));
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_TRUE(testClientConnection()->readBuffer().highWatermarkTriggered());
    EXPECT_FALSE(client_connection_->readEnabled());
  }

  // Drain 3 bytes from the buffer. This bring sit below the low watermark, and
  // read enables, as well as triggering a kick for the remaining byte.
  {
    testClientConnection()->readBuffer().drain(3);
    EXPECT_FALSE(testClientConnection()->readBuffer().highWatermarkTriggered());
    EXPECT_TRUE(client_connection_->readEnabled());

    EXPECT_CALL(*client_read_filter, onData(_, false));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Add 3 bytes to the buffer and verify the connection becomes read disabled
  // again.
  {
    Buffer::OwnedImpl buffer("bye");
    server_connection_->write(buffer, false);
    EXPECT_CALL(*client_read_filter, onData(_, false)).WillOnce(Invoke(on_filter_data_exit));
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_TRUE(testClientConnection()->readBuffer().highWatermarkTriggered());
    EXPECT_FALSE(client_connection_->readEnabled());
  }

  // Now have the consumer read disable.
  // This time when the buffer is drained, there will be no kick as the consumer
  // does not want to read.
  {
    client_connection_->readDisable(true);
    testClientConnection()->readBuffer().drain(3);
    EXPECT_FALSE(testClientConnection()->readBuffer().highWatermarkTriggered());
    EXPECT_FALSE(client_connection_->readEnabled());

    EXPECT_CALL(*client_read_filter, onData(_, false)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Now read enable again.
  // Inside the onData call, readDisable and readEnable. This should trigger
  // another kick on the next dispatcher loop, so onData gets called twice.
  {
    client_connection_->readDisable(false);
    EXPECT_CALL(*client_read_filter, onData(_, false))
        .Times(2)
        .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
          client_connection_->readDisable(true);
          client_connection_->readDisable(false);
          return FilterStatus::StopIteration;
        }))
        .WillRepeatedly(Invoke(on_filter_data_exit));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  // Test the same logic for dispatched_buffered_data from the
  // onReadReady() (read_disable_count_ != 0) path.
  {
    // Fill the buffer and verify the socket is read disabled.
    Buffer::OwnedImpl buffer("bye");
    server_connection_->write(buffer, false);
    EXPECT_CALL(*client_read_filter, onData(_, false))
        .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
          dispatcher_->exit();
          return FilterStatus::StopIteration;
        }));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
    EXPECT_TRUE(testClientConnection()->readBuffer().highWatermarkTriggered());
    EXPECT_FALSE(client_connection_->readEnabled());

    // Read disable and read enable, to set dispatch_buffered_data_ true.
    client_connection_->readDisable(true);
    client_connection_->readDisable(false);
    // Now event loop. This hits the early on-Read path. As above, read
    // disable and read enable from inside the stack of onData, to ensure that
    // dispatch_buffered_data_ works correctly.
    EXPECT_CALL(*client_read_filter, onData(_, false))
        .Times(2)
        .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
          client_connection_->readDisable(true);
          client_connection_->readDisable(false);
          return FilterStatus::StopIteration;
        }))
        .WillRepeatedly(Invoke(on_filter_data_exit));
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  disconnect(true);
}

// Write some data to the connection. It will automatically attempt to flush
// it to the upstream file descriptor via a write() call to buffer_, which is
// configured to succeed and accept all bytes read.
TEST_P(ConnectionImplTest, BasicWrite) {
  useMockBuffer();

  setUpBasicConnection();

  connect();

  // Send the data to the connection and verify it is sent upstream.
  std::string data_to_write = "hello world";
  Buffer::OwnedImpl buffer_to_write(data_to_write);
  std::string data_written;
  EXPECT_CALL(*client_write_buffer_, move(_))
      .WillRepeatedly(DoAll(AddBufferToStringWithoutDraining(&data_written),
                            Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove)));
  EXPECT_CALL(*client_write_buffer_, drain(_))
      .WillOnce(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
  client_connection_->write(buffer_to_write, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(data_to_write, data_written);

  disconnect(true);
}

// Similar to BasicWrite, only with watermarks set.
TEST_P(ConnectionImplTest, WriteWithWatermarks) {
  useMockBuffer();

  setUpBasicConnection();

  connect();

  client_connection_->setBufferLimits(2);

  std::string data_to_write = "hello world";
  Buffer::OwnedImpl first_buffer_to_write(data_to_write);
  std::string data_written;
  EXPECT_CALL(*client_write_buffer_, move(_))
      .WillRepeatedly(DoAll(AddBufferToStringWithoutDraining(&data_written),
                            Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove)));
  EXPECT_CALL(*client_write_buffer_, drain(_))
      .WillOnce(Invoke(client_write_buffer_, &MockWatermarkBuffer::trackDrains));
  // The write() call on the connection will buffer enough data to bring the connection above the
  // high watermark but the subsequent drain immediately brings it back below.
  // A nice future performance optimization would be to latch if the socket is writable in the
  // connection_impl, and try an immediate drain inside of write() to avoid thrashing here.
  EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark());

  client_connection_->write(first_buffer_to_write, false);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(data_to_write, data_written);

  // Now do the write again, but this time configure os_sys_calls to reject the write
  // with errno set to EAGAIN. This should result in going above the high
  // watermark and not returning.
  Buffer::OwnedImpl second_buffer_to_write(data_to_write);
  EXPECT_CALL(*client_write_buffer_, move(_))
      .WillRepeatedly(DoAll(AddBufferToStringWithoutDraining(&data_written),
                            Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove)));
  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, writev(_, _, _))
      .WillOnce(Invoke([&](os_fd_t, const iovec*, int) -> Api::SysCallSizeResult {
        dispatcher_->exit();
        // Return to default os_sys_calls implementation
        os_calls.~TestThreadsafeSingletonInjector();
        return {-1, SOCKET_ERROR_AGAIN};
      }));
  // The write() call on the connection will buffer enough data to bring the connection above the
  // high watermark and as the data will not flush it should not return below the watermark.
  EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark());
  EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(0);
  client_connection_->write(second_buffer_to_write, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Clean up the connection. The close() (called via disconnect) will attempt to flush. The
  // call to write() will succeed, bringing the connection back under the low watermark.
  EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(1);

  disconnect(true);
}

// Read and write random bytes and ensure we don't encounter issues.
TEST_P(ConnectionImplTest, WatermarkFuzzing) {
  useMockBuffer();
  setUpBasicConnection();

  connect();
  client_connection_->setBufferLimits(10);

  TestRandomGenerator rand;
  int bytes_buffered = 0;
  int new_bytes_buffered = 0;

  bool is_below = true;
  bool is_above = false;

  NiceMock<Api::MockOsSysCalls> os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  ON_CALL(os_sys_calls, writev(_, _, _))
      .WillByDefault(Invoke([&](os_fd_t, const iovec*, int) -> Api::SysCallSizeResult {
        return {-1, SOCKET_ERROR_AGAIN};
      }));
  ON_CALL(*client_write_buffer_, drain(_))
      .WillByDefault(testing::Invoke(client_write_buffer_, &MockWatermarkBuffer::baseDrain));
  EXPECT_CALL(*client_write_buffer_, drain(_)).Times(AnyNumber());

  // Randomly write 1-20 bytes and read 1-30 bytes per loop.
  for (int i = 0; i < 50; ++i) {
    // The bytes to read this loop.
    int bytes_to_write = rand.random() % 20 + 1;
    // The bytes buffered at the beginning of this loop.
    bytes_buffered = new_bytes_buffered;
    // Bytes to flush upstream.
    int bytes_to_flush = std::min<int>(rand.random() % 30 + 1, bytes_to_write + bytes_buffered);
    // The number of bytes buffered at the end of this loop.
    new_bytes_buffered = bytes_buffered + bytes_to_write - bytes_to_flush;
    ENVOY_LOG_MISC(trace,
                   "Loop iteration {} bytes_to_write {} bytes_to_flush {} bytes_buffered is {} and "
                   "will be be {}",
                   i, bytes_to_write, bytes_to_flush, bytes_buffered, new_bytes_buffered);

    std::string data(bytes_to_write, 'a');
    Buffer::OwnedImpl buffer_to_write(data);

    // If the current bytes buffered plus the bytes we write this loop go over
    // the watermark and we're not currently above, we will get a callback for
    // going above.
    if (bytes_to_write + bytes_buffered > 11 && is_below) {
      ENVOY_LOG_MISC(trace, "Expect onAboveWriteBufferHighWatermark");
      EXPECT_CALL(client_callbacks_, onAboveWriteBufferHighWatermark());
      is_below = false;
      is_above = true;
    }
    // If after the bytes are flushed upstream the number of bytes remaining is
    // below the low watermark and the bytes were not previously below the low
    // watermark, expect the callback for going below.
    if (new_bytes_buffered <= 5 && is_above) {
      ENVOY_LOG_MISC(trace, "Expect onBelowWriteBufferLowWatermark");
      EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark());
      is_below = true;
      is_above = false;
    }

    // Do the actual work. Write |buffer_to_write| bytes to the connection and
    // drain |bytes_to_flush| before having writev syscall fail with EAGAIN
    EXPECT_CALL(*client_write_buffer_, move(_))
        .WillOnce(Invoke(client_write_buffer_, &MockWatermarkBuffer::baseMove));
    EXPECT_CALL(os_sys_calls, writev(_, _, _))
        .WillOnce(Invoke([&](os_fd_t, const iovec*, int) -> Api::SysCallSizeResult {
          client_write_buffer_->drain(bytes_to_flush);
          return {-1, SOCKET_ERROR_AGAIN};
        }))
        .WillRepeatedly(Invoke([&](os_fd_t, const iovec*, int) -> Api::SysCallSizeResult {
          return {-1, SOCKET_ERROR_AGAIN};
        }));

    client_connection_->write(buffer_to_write, false);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark()).Times(AnyNumber());
  disconnect(true);
}

TEST_P(ConnectionImplTest, BindTest) {
  std::string address_string = TestUtility::getIpv4Loopback();
  if (GetParam() == Network::Address::IpVersion::v4) {
    source_address_ = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance(address_string, 0, nullptr)};
  } else {
    address_string = "::1";
    source_address_ = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv6Instance(address_string, 0, nullptr)};
  }
  setUpBasicConnection();
  connect();
  EXPECT_EQ(address_string, server_connection_->remoteAddress()->ip()->addressAsString());

  disconnect(true);
}

TEST_P(ConnectionImplTest, BindFromSocketTest) {
  std::string address_string = TestUtility::getIpv4Loopback();
  Address::InstanceConstSharedPtr new_source_address;
  if (GetParam() == Network::Address::IpVersion::v4) {
    new_source_address = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance(address_string, 0, nullptr)};
  } else {
    address_string = "::1";
    new_source_address = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv6Instance(address_string, 0, nullptr)};
  }
  auto option = std::make_shared<NiceMock<MockSocketOption>>();
  EXPECT_CALL(*option, setOption(_, Eq(envoy::config::core::v3::SocketOption::STATE_PREBIND)))
      .WillOnce(Invoke([&](Socket& socket, envoy::config::core::v3::SocketOption::SocketState) {
        socket.setLocalAddress(new_source_address);
        return true;
      }));

  socket_options_ = std::make_shared<Socket::Options>();
  socket_options_->emplace_back(std::move(option));
  setUpBasicConnection();
  connect();
  EXPECT_EQ(address_string, server_connection_->remoteAddress()->ip()->addressAsString());

  disconnect(true);
}
TEST_P(ConnectionImplTest, BindFailureTest) {
  // Swap the constraints from BindTest to create an address family mismatch.
  if (GetParam() == Network::Address::IpVersion::v6) {
    const std::string address_string = TestUtility::getIpv4Loopback();
    source_address_ = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance(address_string, 0, nullptr)};
  } else {
    const std::string address_string = "::1";
    source_address_ = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv6Instance(address_string, 0, nullptr)};
  }
  dispatcher_ = api_->allocateDispatcher("test_thread");
  socket_ = std::make_shared<Network::TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr, true);
  listener_ =
      dispatcher_->createListener(socket_, listener_callbacks_, true, ENVOY_TCP_BACKLOG_SIZE);

  client_connection_ = dispatcher_->createClientConnection(
      socket_->localAddress(), source_address_, Network::Test::createRawBufferSocket(), nullptr);

  MockConnectionStats connection_stats;
  client_connection_->setConnectionStats(connection_stats.toBufferStats());
  client_connection_->addConnectionCallbacks(client_callbacks_);
  EXPECT_CALL(connection_stats.bind_errors_, inc());
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

// ReadOnCloseTest verifies that the read filter's onData function is invoked with available data
// when the connection is closed.
TEST_P(ConnectionImplTest, ReadOnCloseTest) {
  setUpBasicConnection();
  connect();

  // Close without flush immediately invokes this callback.
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));

  const int buffer_size = 32;
  Buffer::OwnedImpl data(std::string(buffer_size, 'a'));
  client_connection_->write(data, false);
  client_connection_->close(ConnectionCloseType::NoFlush);

  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(_, _))
      .Times(1)
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) -> FilterStatus {
        EXPECT_EQ(buffer_size, data.length());
        return FilterStatus::StopIteration;
      }));

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// EmptyReadOnCloseTest verifies that the read filter's onData function is not invoked on empty
// read events due to connection closure.
TEST_P(ConnectionImplTest, EmptyReadOnCloseTest) {
  setUpBasicConnection();
  connect();

  // Write some data and verify that the read filter's onData callback is invoked exactly once.
  const int buffer_size = 32;
  Buffer::OwnedImpl data(std::string(buffer_size, 'a'));
  EXPECT_CALL(*read_filter_, onNewConnection());
  EXPECT_CALL(*read_filter_, onData(_, _))
      .Times(1)
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) -> FilterStatus {
        EXPECT_EQ(buffer_size, data.length());
        dispatcher_->exit();
        return FilterStatus::StopIteration;
      }));
  client_connection_->write(data, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  disconnect(true);
}

// Test that a FlushWrite close immediately triggers a close after the write buffer is flushed.
TEST_P(ConnectionImplTest, FlushWriteCloseTest) {
  setUpBasicConnection();
  connect();

  InSequence s1;

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  server_connection_->setDelayedCloseTimeout(std::chrono::milliseconds(100));

  std::shared_ptr<MockReadFilter> client_read_filter(new NiceMock<MockReadFilter>());
  client_connection_->addReadFilter(client_read_filter);

  NiceMockConnectionStats stats;
  server_connection_->setConnectionStats(stats.toBufferStats());

  Buffer::OwnedImpl data("data");
  server_connection_->write(data, false);

  // Server connection flushes the write and immediately closes the socket.
  // There shouldn't be a read/close race here (see issue #2929), since the client is blocked on
  // reading and the connection should close gracefully via FIN.

  EXPECT_CALL(stats.delayed_close_timeouts_, inc()).Times(0);
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose)).Times(1);
  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("data"), false))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&]() -> FilterStatus {
        time_system_.setMonotonicTime(std::chrono::milliseconds(50));
        dispatcher_->exit();
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose)).Times(1);
  server_connection_->close(ConnectionCloseType::FlushWrite);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that a FlushWriteAndDelay close causes Envoy to flush the write and wait for the
// client/peer to close (until a configured timeout which is not expected to trigger in this
// test).
//
// libevent does not provide early close notifications on the currently supported non-Linux
// builds, so the server connection is never notified of the close. For now, we have chosen to
// disable tests that rely on this behavior on macOS and Windows (see
// https://github.com/envoyproxy/envoy/pull/4299).
#if !defined(__APPLE__) && !defined(WIN32)
TEST_P(ConnectionImplTest, FlushWriteAndDelayCloseTest) {
  setUpBasicConnection();
  connect();

  InSequence s1;

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));
  server_connection_->setDelayedCloseTimeout(std::chrono::milliseconds(100));

  std::shared_ptr<MockReadFilter> client_read_filter(new NiceMock<MockReadFilter>());
  client_connection_->addReadFilter(client_read_filter);

  NiceMockConnectionStats stats;
  server_connection_->setConnectionStats(stats.toBufferStats());

  Buffer::OwnedImpl data("Connection: Close");
  server_connection_->write(data, false);

  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("Connection: Close"), false))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&]() -> FilterStatus {
        // Advance time by 50ms; delayed close timer should _not_ trigger.
        time_system_.setMonotonicTime(std::chrono::milliseconds(50));
        client_connection_->close(ConnectionCloseType::NoFlush);
        return FilterStatus::StopIteration;
      }));

  // Client closes the connection so delayed close timer on the server conn should not fire.
  EXPECT_CALL(stats.delayed_close_timeouts_, inc()).Times(0);
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose)).Times(1);
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .Times(1)
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  server_connection_->close(ConnectionCloseType::FlushWriteAndDelay);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}
#endif

// Test that a FlushWriteAndDelay close triggers a timeout which forces Envoy to close the
// connection when a client has not issued a close within the configured interval.
TEST_P(ConnectionImplTest, FlushWriteAndDelayCloseTimerTriggerTest) {
  setUpBasicConnection();
  connect();

  InSequence s1;

  // This timer will be forced to trigger by ensuring time advances by >50ms during the test.
  server_connection_->setDelayedCloseTimeout(std::chrono::milliseconds(50));

  std::shared_ptr<MockReadFilter> client_read_filter(new NiceMock<MockReadFilter>());
  client_connection_->addReadFilter(client_read_filter);

  NiceMockConnectionStats stats;
  server_connection_->setConnectionStats(stats.toBufferStats());

  Buffer::OwnedImpl data("Connection: Close");
  server_connection_->write(data, false);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));

  // The client _will not_ close the connection. Instead, expect the delayed close timer to
  // trigger on the server connection.
  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("Connection: Close"), false))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&]() -> FilterStatus {
        time_system_.setMonotonicTime(std::chrono::milliseconds(100));
        return FilterStatus::StopIteration;
      }));
  server_connection_->close(ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_CALL(stats.delayed_close_timeouts_, inc()).Times(1);
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose)).Times(1);
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .Times(1)
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that a close(FlushWrite) after a delayed close timer has been enabled via
// close(FlushWriteAndDelay) will trigger a socket close after the flush is complete.
TEST_P(ConnectionImplTest, FlushWriteAfterFlushWriteAndDelayWithPendingWrite) {
  setUpBasicConnection();
  connect();

  InSequence s1;
  // The actual timeout is insignificant, we just need to enable delayed close processing by
  // setting it to > 0.
  server_connection_->setDelayedCloseTimeout(std::chrono::milliseconds(50));

  std::shared_ptr<MockReadFilter> client_read_filter(new NiceMock<MockReadFilter>());
  client_connection_->addReadFilter(client_read_filter);
  NiceMockConnectionStats stats;
  server_connection_->setConnectionStats(stats.toBufferStats());

  Buffer::OwnedImpl data("Connection: Close");
  server_connection_->write(data, false);

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));

  // The delayed close timer will be enabled by this call. Data in the write buffer hasn't been
  // flushed yet since the dispatcher has not run.
  server_connection_->close(ConnectionCloseType::FlushWriteAndDelay);
  // The timer won't be disabled but this close() overwrites the delayed close state such that a
  // successful flush will immediately close the socket.
  server_connection_->close(ConnectionCloseType::FlushWrite);

  // The socket close will happen as a result of the write flush and not due to the delayed close
  // timer triggering.
  EXPECT_CALL(stats.delayed_close_timeouts_, inc()).Times(0);
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose)).Times(1);
  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("Connection: Close"), false))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&]() -> FilterStatus {
        time_system_.setMonotonicTime(std::chrono::milliseconds(100));
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .Times(1)
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that a close(FlushWrite) triggers an immediate close when a delayed close timer has been
// enabled via a prior close(FlushWriteAndDelay).
TEST_P(ConnectionImplTest, FlushWriteAfterFlushWriteAndDelayWithoutPendingWrite) {
  setUpBasicConnection();
  connect();

  InSequence s1;
  // The actual timeout is insignificant, we just need to enable delayed close processing by
  // setting it to > 0.
  server_connection_->setDelayedCloseTimeout(std::chrono::milliseconds(50));

  std::shared_ptr<MockReadFilter> client_read_filter(new NiceMock<MockReadFilter>());
  client_connection_->addReadFilter(client_read_filter);
  NiceMockConnectionStats stats;
  server_connection_->setConnectionStats(stats.toBufferStats());

  Buffer::OwnedImpl data("Connection: Close");
  server_connection_->write(data, false);

  server_connection_->close(ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("Connection: Close"), false))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&]() -> FilterStatus {
        dispatcher_->exit();
        return FilterStatus::StopIteration;
      }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // The write buffer has been flushed and a delayed close timer has been set. The socket close
  // will happen as part of the close() since the timeout is no longer required.
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose)).Times(1);
  server_connection_->close(ConnectionCloseType::FlushWrite);
  EXPECT_CALL(stats.delayed_close_timeouts_, inc()).Times(0);
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .Times(1)
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that delayed close processing can be disabled by setting the delayed close timeout
// interval to 0.
TEST_P(ConnectionImplTest, FlushWriteAndDelayConfigDisabledTest) {
  InSequence s1;

  NiceMock<MockConnectionCallbacks> callbacks;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(dispatcher.buffer_factory_, create_(_, _, _))
      .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                std::function<void()> above_overflow) -> Buffer::Instance* {
        return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
      }));
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(0);
  std::unique_ptr<Network::ConnectionImpl> server_connection(new Network::ConnectionImpl(
      dispatcher, std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::make_unique<NiceMock<MockTransportSocket>>(), stream_info_, true));

  time_system_.setMonotonicTime(std::chrono::milliseconds(0));

  // Ensure the delayed close timer is not created when the delayedCloseTimeout config value is
  // set to 0.
  server_connection->setDelayedCloseTimeout(std::chrono::milliseconds(0));
  EXPECT_CALL(dispatcher, createTimer_(_)).Times(0);

  NiceMockConnectionStats stats;
  server_connection->setConnectionStats(stats.toBufferStats());

  EXPECT_CALL(stats.delayed_close_timeouts_, inc()).Times(0);
  server_connection->close(ConnectionCloseType::FlushWriteAndDelay);
  // Advance time by a value larger than the delayed close timeout default (1000ms). This would
  // trigger the delayed close timer callback if set.
  time_system_.setMonotonicTime(std::chrono::milliseconds(10000));

  // Since the delayed close timer never triggers, the connection never closes. Close it here to
  // end the test cleanly due to the (fd == -1) assert in ~ConnectionImpl().
  server_connection->close(ConnectionCloseType::NoFlush);
}

// Test that the delayed close timer is reset while write flushes are happening when a connection
// is in delayed close mode.
TEST_P(ConnectionImplTest, DelayedCloseTimerResetWithPendingWriteBufferFlushes) {
  ConnectionMocks mocks = createConnectionMocks();
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(0);
  auto server_connection = std::make_unique<Network::ConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_, true);

#ifndef NDEBUG
  // Ignore timer enabled() calls used to check timer state in ASSERTs.
  EXPECT_CALL(*mocks.timer_, enabled()).Times(AnyNumber());
#endif

  InSequence s1;
  // The actual timeout is insignificant, we just need to enable delayed close processing by
  // setting it to > 0.
  auto timeout = std::chrono::milliseconds(100);
  server_connection->setDelayedCloseTimeout(timeout);

  EXPECT_CALL(*mocks.file_event_, activate(Event::FileReadyType::Write))
      .WillOnce(Invoke(*mocks.file_ready_cb_));
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual("data"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> IoResult {
        // Do not drain the buffer and return 0 bytes processed to simulate backpressure.
        return IoResult{PostIoAction::KeepOpen, 0, false};
      }));
  Buffer::OwnedImpl data("data");
  server_connection->write(data, false);

  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _)).Times(1);
  server_connection->close(ConnectionCloseType::FlushWriteAndDelay);

  // The write ready event cb (ConnectionImpl::onWriteReady()) will reset the timer to its
  // original timeout value to avoid triggering while the write buffer is being actively flushed.
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual("data"), _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> IoResult {
        // Partial flush.
        uint64_t bytes_drained = 1;
        buffer.drain(bytes_drained);
        return IoResult{PostIoAction::KeepOpen, bytes_drained, false};
      }));
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _)).Times(1);
  (*mocks.file_ready_cb_)(Event::FileReadyType::Write);

  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual("ata"), _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> IoResult {
        // Flush the entire buffer.
        uint64_t bytes_drained = buffer.length();
        buffer.drain(buffer.length());
        return IoResult{PostIoAction::KeepOpen, bytes_drained, false};
      }));
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _)).Times(1);
  (*mocks.file_ready_cb_)(Event::FileReadyType::Write);

  // Force the delayed close timeout to trigger so the connection is cleaned up.
  mocks.timer_->invokeCallback();
}

// Test that the delayed close timer is not reset by spurious fd Write events that either consume 0
// bytes from the output buffer or are delivered after close(FlushWriteAndDelay).
TEST_P(ConnectionImplTest, IgnoreSpuriousFdWriteEventsDuringFlushWriteAndDelay) {
  ConnectionMocks mocks = createConnectionMocks();
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(0);
  auto server_connection = std::make_unique<Network::ConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_, true);

#ifndef NDEBUG
  // Ignore timer enabled() calls used to check timer state in ASSERTs.
  EXPECT_CALL(*mocks.timer_, enabled()).Times(AnyNumber());
#endif

  InSequence s1;
  // The actual timeout is insignificant, we just need to enable delayed close processing by
  // setting it to > 0.
  auto timeout = std::chrono::milliseconds(100);
  server_connection->setDelayedCloseTimeout(timeout);

  EXPECT_CALL(*mocks.file_event_, activate(Event::FileReadyType::Write))
      .WillOnce(Invoke(*mocks.file_ready_cb_));
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual("data"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> IoResult {
        // Do not drain the buffer and return 0 bytes processed to simulate backpressure.
        return IoResult{PostIoAction::KeepOpen, 0, false};
      }));
  Buffer::OwnedImpl data("data");
  server_connection->write(data, false);

  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _)).Times(1);
  server_connection->close(ConnectionCloseType::FlushWriteAndDelay);

  // The write ready event cb (ConnectionImpl::onWriteReady()) will reset the timer to its
  // original timeout value to avoid triggering while the write buffer is being actively flushed.
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual("data"), _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> IoResult {
        // Partial flush.
        uint64_t bytes_drained = 1;
        buffer.drain(bytes_drained);
        return IoResult{PostIoAction::KeepOpen, bytes_drained, false};
      }));
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _)).Times(1);
  (*mocks.file_ready_cb_)(Event::FileReadyType::Write);

  // Handle a write event and drain 0 bytes from the buffer. Verify that the timer is not reset.
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual("ata"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> IoResult {
        // Don't consume any bytes.
        return IoResult{PostIoAction::KeepOpen, 0, false};
      }));
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _)).Times(0);
  (*mocks.file_ready_cb_)(Event::FileReadyType::Write);

  // Handle a write event and drain the remainder of the buffer. Verify that the timer is reset.
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual("ata"), _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> IoResult {
        // Flush the entire buffer.
        ASSERT(buffer.length() > 0);
        uint64_t bytes_drained = buffer.length();
        buffer.drain(buffer.length());
        EXPECT_EQ(server_connection->state(), Connection::State::Closing);
        return IoResult{PostIoAction::KeepOpen, bytes_drained, false};
      }));
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _)).Times(1);
  (*mocks.file_ready_cb_)(Event::FileReadyType::Write);

  // Handle a write event after entering the half-closed state. Verify that the timer is not reset
  // because write consumed 0 bytes from the empty buffer.
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual(""), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> IoResult {
        EXPECT_EQ(server_connection->state(), Connection::State::Closing);
        return IoResult{PostIoAction::KeepOpen, 0, false};
      }));
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _)).Times(0);
  (*mocks.file_ready_cb_)(Event::FileReadyType::Write);

  // Handle a write event that somehow drains bytes from an empty output buffer. Since
  // some bytes were consumed, the timer is reset.
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual(""), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> IoResult {
        EXPECT_EQ(server_connection->state(), Connection::State::Closing);
        return IoResult{PostIoAction::KeepOpen, 1, false};
      }));
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _)).Times(1);
  (*mocks.file_ready_cb_)(Event::FileReadyType::Write);

  // Force the delayed close timeout to trigger so the connection is cleaned up.
  mocks.timer_->invokeCallback();
}

// Test that tearing down the connection will disable the delayed close timer.
TEST_P(ConnectionImplTest, DelayedCloseTimeoutDisableOnSocketClose) {
  ConnectionMocks mocks = createConnectionMocks();
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(0);
  auto server_connection = std::make_unique<Network::ConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_, true);

  InSequence s1;

  // The actual timeout is insignificant, we just need to enable delayed close processing by
  // setting it to > 0.
  server_connection->setDelayedCloseTimeout(std::chrono::milliseconds(100));

  Buffer::OwnedImpl data("data");
  EXPECT_CALL(*mocks.file_event_, activate(Event::FileReadyType::Write))
      .WillOnce(Invoke(*mocks.file_ready_cb_));
  // The buffer must be drained when write() is called on the connection to allow the close() to
  // enable the timer.
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual("data"), _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> IoResult {
        buffer.drain(buffer.length());
        return IoResult{PostIoAction::KeepOpen, buffer.length(), false};
      }));
  server_connection->write(data, false);
  EXPECT_CALL(*mocks.timer_, enableTimer(_, _)).Times(1);
  // Enable the delayed close timer.
  server_connection->close(ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_CALL(*mocks.timer_, disableTimer()).Times(1);
  // This close() will call closeSocket(), which should disable the timer to avoid triggering it
  // after the connection's data structures have been reset.
  server_connection->close(ConnectionCloseType::NoFlush);
}

// Test that the delayed close timeout callback is resilient to connection teardown edge cases.
TEST_P(ConnectionImplTest, DelayedCloseTimeoutNullStats) {
  ConnectionMocks mocks = createConnectionMocks();
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(0);
  auto server_connection = std::make_unique<Network::ConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_, true);

  InSequence s1;

  // The actual timeout is insignificant, we just need to enable delayed close processing by
  // setting it to > 0.
  server_connection->setDelayedCloseTimeout(std::chrono::milliseconds(100));

  // NOTE: Avoid providing stats storage to the connection via setConnectionStats(). This
  // guarantees that connection_stats_ is a nullptr and that the callback resiliency validation
  // below tests that edge case.

  Buffer::OwnedImpl data("data");
  EXPECT_CALL(*mocks.file_event_, activate(Event::FileReadyType::Write))
      .WillOnce(Invoke(*mocks.file_ready_cb_));
  // The buffer must be drained when write() is called on the connection to allow the close() to
  // enable the timer.
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual("data"), _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> IoResult {
        buffer.drain(buffer.length());
        return IoResult{PostIoAction::KeepOpen, buffer.length(), false};
      }));
  server_connection->write(data, false);

  EXPECT_CALL(*mocks.timer_, enableTimer(_, _)).Times(1);
  server_connection->close(ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_CALL(*mocks.timer_, disableTimer()).Times(1);
  // The following close() will call closeSocket() and reset internal data structures such as
  // stats.
  server_connection->close(ConnectionCloseType::NoFlush);
}

class FakeReadFilter : public Network::ReadFilter {
public:
  FakeReadFilter() = default;
  ~FakeReadFilter() override {
    EXPECT_TRUE(callbacks_ != nullptr);
    // The purpose is to verify that when FilterManger is destructed, ConnectionSocketImpl is not
    // destructed, and ConnectionSocketImpl can still be accessed via ReadFilterCallbacks.
    EXPECT_TRUE(callbacks_->connection().state() != Network::Connection::State::Open);
  }

  Network::FilterStatus onData(Buffer::Instance& data, bool) override {
    data.drain(data.length());
    return Network::FilterStatus::Continue;
  }

  Network::FilterStatus onNewConnection() override { return Network::FilterStatus::Continue; }

  void initializeReadFilterCallbacks(ReadFilterCallbacks& callbacks) override {
    callbacks_ = &callbacks;
  }

private:
  ReadFilterCallbacks* callbacks_{nullptr};
};

class MockTransportConnectionImplTest : public testing::Test {
public:
  MockTransportConnectionImplTest() : stream_info_(dispatcher_.timeSource()) {
    EXPECT_CALL(dispatcher_.buffer_factory_, create_(_, _, _))
        .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                  std::function<void()> above_overflow) -> Buffer::Instance* {
          return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
        }));

    file_event_ = new Event::MockFileEvent;
    EXPECT_CALL(dispatcher_, createFileEvent_(0, _, _, _))
        .WillOnce(DoAll(SaveArg<1>(&file_ready_cb_), Return(file_event_)));
    transport_socket_ = new NiceMock<MockTransportSocket>;
    EXPECT_CALL(*transport_socket_, setTransportSocketCallbacks(_))
        .WillOnce(Invoke([this](TransportSocketCallbacks& callbacks) {
          transport_socket_callbacks_ = &callbacks;
        }));
    IoHandlePtr io_handle = std::make_unique<IoSocketHandleImpl>(0);
    connection_ = std::make_unique<ConnectionImpl>(
        dispatcher_, std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
        TransportSocketPtr(transport_socket_), stream_info_, true);
    connection_->addConnectionCallbacks(callbacks_);
  }

  ~MockTransportConnectionImplTest() override { connection_->close(ConnectionCloseType::NoFlush); }

  // This may be invoked for doWrite() on the transport to simulate all the data
  // being written.
  static IoResult SimulateSuccessfulWrite(Buffer::Instance& buffer, bool) {
    uint64_t size = buffer.length();
    buffer.drain(size);
    return {PostIoAction::KeepOpen, size, false};
  }

  std::unique_ptr<ConnectionImpl> connection_;
  Event::MockDispatcher dispatcher_;
  NiceMock<MockConnectionCallbacks> callbacks_;
  MockTransportSocket* transport_socket_;
  Event::MockFileEvent* file_event_;
  Event::FileReadyCb file_ready_cb_;
  TransportSocketCallbacks* transport_socket_callbacks_;
  StreamInfo::StreamInfoImpl stream_info_;
};

// The purpose of this case is to verify the destructor order of the object.
// FilterManager relies on ConnectionSocketImpl, so the FilterManager can be
// destructed after the ConnectionSocketImpl is destructed.
//
// Ref: https://github.com/envoyproxy/envoy/issues/5313
TEST_F(MockTransportConnectionImplTest, ObjectDestructOrder) {
  connection_->addReadFilter(std::make_shared<Network::FakeReadFilter>());
  connection_->enableHalfClose(true);
  EXPECT_CALL(*transport_socket_, doRead(_))
      .Times(2)
      .WillRepeatedly(Return(IoResult{PostIoAction::KeepOpen, 0, true}));
  file_ready_cb_(Event::FileReadyType::Read);
  file_ready_cb_(Event::FileReadyType::Read);
}

// Test that BytesSentCb is invoked at the correct times
TEST_F(MockTransportConnectionImplTest, BytesSentCallback) {
  uint64_t bytes_sent = 0;
  uint64_t cb_called = 0;
  connection_->addBytesSentCallback([&](uint64_t arg) {
    cb_called++;
    bytes_sent = arg;
  });

  // 100 bytes were sent; expect BytesSent event
  EXPECT_CALL(*transport_socket_, doWrite(_, _))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 100, false}));
  file_ready_cb_(Event::FileReadyType::Write);
  EXPECT_EQ(cb_called, 1);
  EXPECT_EQ(bytes_sent, 100);
  cb_called = false;
  bytes_sent = 0;

  // 0 bytes were sent; no event
  EXPECT_CALL(*transport_socket_, doWrite(_, _))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  file_ready_cb_(Event::FileReadyType::Write);
  EXPECT_EQ(cb_called, 0);

  // Reading should not cause BytesSent
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 1, false}));
  file_ready_cb_(Event::FileReadyType::Read);
  EXPECT_EQ(cb_called, 0);

  // Closed event should not raise a BytesSent event (but does raise RemoteClose)
  EXPECT_CALL(callbacks_, onEvent(ConnectionEvent::RemoteClose));
  file_ready_cb_(Event::FileReadyType::Closed);
  EXPECT_EQ(cb_called, 0);
}

// Make sure that multiple registered callbacks all get called
TEST_F(MockTransportConnectionImplTest, BytesSentMultiple) {
  uint64_t cb_called1 = 0;
  uint64_t cb_called2 = 0;
  uint64_t bytes_sent1 = 0;
  uint64_t bytes_sent2 = 0;
  connection_->addBytesSentCallback([&](uint64_t arg) {
    cb_called1++;
    bytes_sent1 = arg;
  });

  connection_->addBytesSentCallback([&](uint64_t arg) {
    cb_called2++;
    bytes_sent2 = arg;
  });

  EXPECT_CALL(*transport_socket_, doWrite(_, _))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 100, false}));
  file_ready_cb_(Event::FileReadyType::Write);
  EXPECT_EQ(cb_called1, 1);
  EXPECT_EQ(cb_called2, 1);
  EXPECT_EQ(bytes_sent1, 100);
  EXPECT_EQ(bytes_sent2, 100);
}

// Test that if a callback closes the connection, further callbacks are not called.
TEST_F(MockTransportConnectionImplTest, BytesSentCloseInCallback) {
  // Order is not defined, so register two callbacks that both close the connection. Only
  // one of them should be called.
  uint64_t cb_called = 0;
  Connection::BytesSentCb cb = [&](uint64_t) {
    cb_called++;
    connection_->close(ConnectionCloseType::NoFlush);
  };
  connection_->addBytesSentCallback(cb);
  connection_->addBytesSentCallback(cb);

  EXPECT_CALL(*transport_socket_, doWrite(_, _))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 100, false}));
  file_ready_cb_(Event::FileReadyType::Write);

  EXPECT_EQ(cb_called, 1);
  EXPECT_EQ(connection_->state(), Connection::State::Closed);
}

// Test that onWrite does not have end_stream set, with half-close disabled
TEST_F(MockTransportConnectionImplTest, FullCloseWrite) {
  const std::string val("some data");
  Buffer::OwnedImpl buffer(val);
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke(file_ready_cb_));
  EXPECT_CALL(*transport_socket_, doWrite(BufferStringEqual(val), false))
      .WillOnce(Invoke(SimulateSuccessfulWrite));
  connection_->write(buffer, false);
}

// Test that onWrite has end_stream set correctly, with half-close enabled
TEST_F(MockTransportConnectionImplTest, HalfCloseWrite) {
  connection_->enableHalfClose(true);
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write))
      .WillRepeatedly(Invoke(file_ready_cb_));

  const std::string val("some data");
  Buffer::OwnedImpl buffer(val);
  EXPECT_CALL(*transport_socket_, doWrite(BufferStringEqual(val), false))
      .WillOnce(Invoke(SimulateSuccessfulWrite));
  connection_->write(buffer, false);

  EXPECT_CALL(*transport_socket_, doWrite(_, true)).WillOnce(Invoke(SimulateSuccessfulWrite));
  connection_->write(buffer, true);
}

TEST_F(MockTransportConnectionImplTest, ReadMultipleEndStream) {
  std::shared_ptr<MockReadFilter> read_filter(new NiceMock<MockReadFilter>());
  connection_->enableHalfClose(true);
  connection_->addReadFilter(read_filter);
  EXPECT_CALL(*transport_socket_, doRead(_))
      .Times(2)
      .WillRepeatedly(Return(IoResult{PostIoAction::KeepOpen, 0, true}));
  EXPECT_CALL(*read_filter, onData(_, true)).Times(1);
  file_ready_cb_(Event::FileReadyType::Read);
  file_ready_cb_(Event::FileReadyType::Read);
}

// Test that if both sides half-close, the connection is closed, with the read half-close coming
// first.
TEST_F(MockTransportConnectionImplTest, BothHalfCloseReadFirst) {
  std::shared_ptr<MockReadFilter> read_filter(new NiceMock<MockReadFilter>());
  connection_->enableHalfClose(true);
  connection_->addReadFilter(read_filter);

  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, true}));
  file_ready_cb_(Event::FileReadyType::Read);

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(*transport_socket_, doWrite(_, true))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke(file_ready_cb_));
  EXPECT_CALL(callbacks_, onEvent(ConnectionEvent::LocalClose));
  connection_->write(buffer, true);
}

// Test that if both sides half-close, the connection is closed, with the write half-close coming
// first.
TEST_F(MockTransportConnectionImplTest, BothHalfCloseWriteFirst) {
  std::shared_ptr<MockReadFilter> read_filter(new NiceMock<MockReadFilter>());
  connection_->enableHalfClose(true);
  connection_->addReadFilter(read_filter);

  Buffer::OwnedImpl buffer;
  EXPECT_CALL(*transport_socket_, doWrite(_, true))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke(file_ready_cb_));
  connection_->write(buffer, true);

  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, true}));
  EXPECT_CALL(callbacks_, onEvent(ConnectionEvent::RemoteClose));
  file_ready_cb_(Event::FileReadyType::Read);
}

// Test that if both sides half-close, but writes have not yet been written to the Transport, that
// the connection closes only when the writes complete flushing. The write half-close happens
// first.
TEST_F(MockTransportConnectionImplTest, BothHalfCloseWritesNotFlushedWriteFirst) {
  std::shared_ptr<MockReadFilter> read_filter(new NiceMock<MockReadFilter>());
  connection_->enableHalfClose(true);
  connection_->addReadFilter(read_filter);

  Buffer::OwnedImpl buffer("data");
  EXPECT_CALL(*transport_socket_, doWrite(_, true))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke(file_ready_cb_));
  connection_->write(buffer, true);

  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, true}));
  file_ready_cb_(Event::FileReadyType::Read);

  EXPECT_CALL(callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(*transport_socket_, doWrite(_, true)).WillOnce(Invoke(SimulateSuccessfulWrite));
  file_ready_cb_(Event::FileReadyType::Write);
}

// Test that if both sides half-close, but writes have not yet been written to the Transport, that
// the connection closes only when the writes complete flushing. The read half-close happens
// first.
TEST_F(MockTransportConnectionImplTest, BothHalfCloseWritesNotFlushedReadFirst) {
  std::shared_ptr<MockReadFilter> read_filter(new NiceMock<MockReadFilter>());
  connection_->enableHalfClose(true);
  connection_->addReadFilter(read_filter);

  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, true}));
  file_ready_cb_(Event::FileReadyType::Read);

  Buffer::OwnedImpl buffer("data");
  EXPECT_CALL(*transport_socket_, doWrite(_, true))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke(file_ready_cb_));
  connection_->write(buffer, true);

  EXPECT_CALL(*transport_socket_, doWrite(_, true))
      .WillOnce(Invoke([](Buffer::Instance& data, bool) -> IoResult {
        uint64_t len = data.length();
        data.drain(len);
        return {PostIoAction::KeepOpen, len, false};
      }));
  EXPECT_CALL(callbacks_, onEvent(ConnectionEvent::LocalClose));
  file_ready_cb_(Event::FileReadyType::Write);
}

// Test that if end_stream is raised, but a filter stops iteration, that end_stream
// propagates correctly.
TEST_F(MockTransportConnectionImplTest, ReadEndStreamStopIteration) {
  const std::string val("a");
  std::shared_ptr<MockReadFilter> read_filter1(new StrictMock<MockReadFilter>());
  std::shared_ptr<MockReadFilter> read_filter2(new StrictMock<MockReadFilter>());
  connection_->enableHalfClose(true);
  connection_->addReadFilter(read_filter1);
  connection_->addReadFilter(read_filter2);

  EXPECT_CALL(*read_filter1, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*read_filter2, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Invoke([val](Buffer::Instance& buffer) -> IoResult {
        buffer.add(val.c_str(), val.size());
        return {PostIoAction::KeepOpen, val.size(), true};
      }));

  EXPECT_CALL(*read_filter1, onData(BufferStringEqual(val), true))
      .WillOnce(Return(FilterStatus::StopIteration));
  file_ready_cb_(Event::FileReadyType::Read);

  EXPECT_CALL(*read_filter2, onData(BufferStringEqual(val), true))
      .WillOnce(Return(FilterStatus::StopIteration));
  read_filter1->callbacks_->continueReading();
}

// Test that if end_stream is written, but a filter stops iteration, that end_stream
// propagates correctly.
TEST_F(MockTransportConnectionImplTest, WriteEndStreamStopIteration) {
  const std::string val("a");
  std::shared_ptr<MockWriteFilter> write_filter1(new StrictMock<MockWriteFilter>());
  std::shared_ptr<MockWriteFilter> write_filter2(new StrictMock<MockWriteFilter>());
  connection_->enableHalfClose(true);
  connection_->addWriteFilter(write_filter2);
  connection_->addWriteFilter(write_filter1);

  EXPECT_CALL(*write_filter1, onWrite(BufferStringEqual(val), true))
      .WillOnce(Return(FilterStatus::StopIteration));
  Buffer::OwnedImpl buffer(val);
  connection_->write(buffer, true);

  EXPECT_CALL(*write_filter1, onWrite(BufferStringEqual(val), true))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*write_filter2, onWrite(BufferStringEqual(val), true))
      .WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write));
  connection_->write(buffer, true);
}

// Validate that when the transport signals ConnectionEvent::Connected, that we
// check for pending write buffer content.
TEST_F(MockTransportConnectionImplTest, WriteReadyOnConnected) {
  InSequence s;

  // Queue up some data in write buffer, simulating what happens in SSL handshake.
  const std::string val("some data");
  Buffer::OwnedImpl buffer(val);
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke(file_ready_cb_));
  EXPECT_CALL(*transport_socket_, doWrite(BufferStringEqual(val), false))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  connection_->write(buffer, false);

  // A read event happens, resulting in handshake completion and
  // raiseEvent(Network::ConnectionEvent::Connected). Since we have data queued
  // in the write buffer, we should see a doWrite with this data.
  EXPECT_CALL(*transport_socket_, doRead(_)).WillOnce(InvokeWithoutArgs([this] {
    transport_socket_callbacks_->raiseEvent(Network::ConnectionEvent::Connected);
    return IoResult{PostIoAction::KeepOpen, 0, false};
  }));
  EXPECT_CALL(*transport_socket_, doWrite(BufferStringEqual(val), false))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  file_ready_cb_(Event::FileReadyType::Read);
  EXPECT_CALL(*transport_socket_, doWrite(_, true))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, true}));
}

// Test the interface used by external consumers.
TEST_F(MockTransportConnectionImplTest, FlushWriteBuffer) {
  InSequence s;

  // Queue up some data in write buffer.
  const std::string val("some data");
  Buffer::OwnedImpl buffer(val);
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke(file_ready_cb_));
  EXPECT_CALL(*transport_socket_, doWrite(BufferStringEqual(val), false))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  connection_->write(buffer, false);

  // A read event triggers underlying socket to ask for more data.
  EXPECT_CALL(*transport_socket_, doRead(_)).WillOnce(InvokeWithoutArgs([this] {
    transport_socket_callbacks_->flushWriteBuffer();
    return IoResult{PostIoAction::KeepOpen, 0, false};
  }));
  EXPECT_CALL(*transport_socket_, doWrite(BufferStringEqual(val), false))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  file_ready_cb_(Event::FileReadyType::Read);
  EXPECT_CALL(*transport_socket_, doWrite(_, true))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, true}));
}

// Fixture for validating behavior after a connection is closed.
class PostCloseConnectionImplTest : public MockTransportConnectionImplTest {
protected:
  // Setup connection, single read event.
  void initialize() {
    connection_->addReadFilter(read_filter_);
    connection_->setDelayedCloseTimeout(std::chrono::milliseconds(100));

    EXPECT_CALL(*transport_socket_, doRead(_))
        .WillOnce(Invoke([this](Buffer::Instance& buffer) -> IoResult {
          buffer.add(val_.c_str(), val_.size());
          return {PostIoAction::KeepOpen, val_.size(), false};
        }));
    EXPECT_CALL(*read_filter_, onNewConnection());
    EXPECT_CALL(*read_filter_, onData(_, _));
    file_ready_cb_(Event::FileReadyType::Read);
  }

  void writeSomeData() {
    Buffer::OwnedImpl buffer("data");
    EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write));
    connection_->write(buffer, false);
  }

  const std::string val_{"a"};
  std::shared_ptr<MockReadFilter> read_filter_{new StrictMock<MockReadFilter>()};
};

// Test that if a read event occurs after
// close(ConnectionCloseType::FlushWriteAndDelay), the read is not propagated to
// a read filter.
TEST_F(PostCloseConnectionImplTest, ReadAfterCloseFlushWriteDelayIgnored) {
  InSequence s;
  initialize();

  // Delayed connection close.
  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(*file_event_, setEnabled(Event::FileReadyType::Closed));
  connection_->close(ConnectionCloseType::FlushWriteAndDelay);

  // Read event, doRead() happens on connection but no filter onData().
  EXPECT_CALL(*read_filter_, onData(_, _)).Times(0);
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Invoke([this](Buffer::Instance& buffer) -> IoResult {
        buffer.add(val_.c_str(), val_.size());
        return {PostIoAction::KeepOpen, val_.size(), false};
      }));
  file_ready_cb_(Event::FileReadyType::Read);
  // Deferred close.
  EXPECT_CALL(*transport_socket_, closeSocket(_));
}

// Test that if a read event occurs after
// close(ConnectionCloseType::FlushWriteAndDelay) with pending write data, the
// read is not propagated to a read filter.
TEST_F(PostCloseConnectionImplTest, ReadAfterCloseFlushWriteDelayIgnoredWithWriteData) {
  InSequence s;
  initialize();
  writeSomeData();

  // Delayed connection close.
  EXPECT_CALL(dispatcher_, createTimer_(_));
  // With half-close semantics enabled we will not wait for early close notification.
  // See the `Envoy::Network::ConnectionImpl::readDisable()' method for more details.
  EXPECT_CALL(*file_event_, setEnabled(0));
  connection_->enableHalfClose(true);
  connection_->close(ConnectionCloseType::FlushWriteAndDelay);

  // Read event, doRead() happens on connection but no filter onData().
  EXPECT_CALL(*read_filter_, onData(_, _)).Times(0);
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Invoke([this](Buffer::Instance& buffer) -> IoResult {
        buffer.add(val_.c_str(), val_.size());
        return {PostIoAction::KeepOpen, val_.size(), false};
      }));
  file_ready_cb_(Event::FileReadyType::Read);
  // We have data written above in writeSomeData(), it will be flushed here.
  EXPECT_CALL(*transport_socket_, doWrite(_, true))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  // Deferred close.
  EXPECT_CALL(*transport_socket_, closeSocket(_));
}

// Test that if a read event occurs after
// close(ConnectionCloseType::FlushWriteAndDelay) with pending write data and a
// transport socket than canFlushClose(), the read is not propagated to a read
// filter.
TEST_F(PostCloseConnectionImplTest, ReadAfterCloseFlushWriteDelayIgnoredCanFlushClose) {
  InSequence s;
  initialize();
  writeSomeData();

  // The path of interest is when the transport socket canFlushClose().
  ON_CALL(*transport_socket_, canFlushClose()).WillByDefault(Return(true));

  // Delayed connection close.
  EXPECT_CALL(dispatcher_, createTimer_(_));
  EXPECT_CALL(*file_event_, setEnabled(Event::FileReadyType::Write | Event::FileReadyType::Closed));
  connection_->close(ConnectionCloseType::FlushWriteAndDelay);

  // Read event, doRead() happens on connection but no filter onData().
  EXPECT_CALL(*read_filter_, onData(_, _)).Times(0);
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Invoke([this](Buffer::Instance& buffer) -> IoResult {
        buffer.add(val_.c_str(), val_.size());
        return {PostIoAction::KeepOpen, val_.size(), false};
      }));
  file_ready_cb_(Event::FileReadyType::Read);

  // Deferred close.
  EXPECT_CALL(*transport_socket_, closeSocket(_));
}

// Test that if a read event occurs after close(ConnectionCloseType::NoFlush),
// then no read is attempted from the transport socket and hence the read is not
// propagated to a read filter.
TEST_F(PostCloseConnectionImplTest, NoReadAfterCloseNoFlush) {
  InSequence s;
  initialize();

  // Immediate connection close.
  EXPECT_CALL(*transport_socket_, closeSocket(_));
  connection_->close(ConnectionCloseType::NoFlush);

  // We don't even see a doRead(), let alone an onData() callback.
  EXPECT_CALL(*read_filter_, onData(_, _)).Times(0);
  EXPECT_CALL(*transport_socket_, doRead(_)).Times(0);
  file_ready_cb_(Event::FileReadyType::Read);
}

// Test that if a read event occurs after close(ConnectionCloseType::FlushWrite),
// then no read is attempted from the transport socket and hence the read is not
// propagated to a read filter.
TEST_F(PostCloseConnectionImplTest, NoReadAfterCloseFlushWrite) {
  InSequence s;
  initialize();

  // Connection flush and close.
  EXPECT_CALL(*transport_socket_, closeSocket(_));
  connection_->close(ConnectionCloseType::FlushWrite);

  // We don't even see a doRead(), let alone an onData() callback.
  EXPECT_CALL(*read_filter_, onData(_, _)).Times(0);
  EXPECT_CALL(*transport_socket_, doRead(_)).Times(0);
  file_ready_cb_(Event::FileReadyType::Read);
}

// Test that if a read event occurs after close(ConnectionCloseType::FlushWrite)
// with pending write data, then no read is attempted from the transport socket
// and hence the read is not propagated to a read filter.
TEST_F(PostCloseConnectionImplTest, NoReadAfterCloseFlushWriteWriteData) {
  InSequence s;
  initialize();
  writeSomeData();

  // Connection flush and close. We have data written above in writeSomeData(),
  // it will be flushed here.
  EXPECT_CALL(*transport_socket_, doWrite(_, true))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  EXPECT_CALL(*transport_socket_, closeSocket(_));
  connection_->close(ConnectionCloseType::FlushWrite);

  // We don't even see a doRead(), let alone an onData() callback.
  EXPECT_CALL(*read_filter_, onData(_, _)).Times(0);
  EXPECT_CALL(*transport_socket_, doRead(_)).Times(0);
  file_ready_cb_(Event::FileReadyType::Read);
}

class ReadBufferLimitTest : public ConnectionImplTest {
public:
  void readBufferLimitTest(uint32_t read_buffer_limit, uint32_t expected_chunk_size) {
    const uint32_t buffer_size = 256 * 1024;
    dispatcher_ = api_->allocateDispatcher("test_thread");
    socket_ = std::make_shared<Network::TcpListenSocket>(
        Network::Test::getCanonicalLoopbackAddress(GetParam()), nullptr, true);
    listener_ =
        dispatcher_->createListener(socket_, listener_callbacks_, true, ENVOY_TCP_BACKLOG_SIZE);

    client_connection_ = dispatcher_->createClientConnection(
        socket_->localAddress(), Network::Address::InstanceConstSharedPtr(),
        Network::Test::createRawBufferSocket(), nullptr);
    client_connection_->addConnectionCallbacks(client_callbacks_);
    client_connection_->connect();

    read_filter_ = std::make_shared<NiceMock<MockReadFilter>>();
    EXPECT_CALL(listener_callbacks_, onAccept_(_))
        .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
          server_connection_ = dispatcher_->createServerConnection(
              std::move(socket), Network::Test::createRawBufferSocket(), stream_info_);
          server_connection_->setBufferLimits(read_buffer_limit);
          server_connection_->addReadFilter(read_filter_);
          EXPECT_EQ("", server_connection_->nextProtocol());
          EXPECT_EQ(read_buffer_limit, server_connection_->bufferLimit());
        }));

    uint32_t filter_seen = 0;

    EXPECT_CALL(*read_filter_, onNewConnection());
    EXPECT_CALL(*read_filter_, onData(_, _))
        .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) -> FilterStatus {
          EXPECT_GE(expected_chunk_size, data.length());
          filter_seen += data.length();
          data.drain(data.length());
          if (filter_seen == buffer_size) {
            server_connection_->close(ConnectionCloseType::FlushWrite);
          }
          return FilterStatus::StopIteration;
        }));

    EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
        .WillOnce(Invoke([&](Network::ConnectionEvent) -> void {
          EXPECT_EQ(buffer_size, filter_seen);
          dispatcher_->exit();
        }));

    Buffer::OwnedImpl data(std::string(buffer_size, 'a'));
    client_connection_->write(data, false);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ReadBufferLimitTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(ReadBufferLimitTest, NoLimit) { readBufferLimitTest(0, 256 * 1024); }

TEST_P(ReadBufferLimitTest, SomeLimit) {
  const uint32_t read_buffer_limit = 32 * 1024;
  // Envoy has soft limits, so as long as the first read is <= read_buffer_limit - 1 it will do a
  // second read. The effective chunk size is then read_buffer_limit - 1 + MaxReadSize,
  // which is currently 16384.
  readBufferLimitTest(read_buffer_limit, read_buffer_limit - 1 + 16384);
}

class TcpClientConnectionImplTest : public testing::TestWithParam<Address::IpVersion> {
protected:
  TcpClientConnectionImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
};
INSTANTIATE_TEST_SUITE_P(IpVersions, TcpClientConnectionImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(TcpClientConnectionImplTest, BadConnectNotConnRefused) {
  Address::InstanceConstSharedPtr address;
  if (GetParam() == Network::Address::IpVersion::v4) {
    // Connecting to 255.255.255.255 will cause a perm error and not ECONNREFUSED which is a
    // different path in libevent. Make sure this doesn't crash.
    address = Utility::resolveUrl("tcp://255.255.255.255:1");
  } else {
    // IPv6 reserved multicast address.
    address = Utility::resolveUrl("tcp://[ff00::]:1");
  }
  ClientConnectionPtr connection =
      dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                          Network::Test::createRawBufferSocket(), nullptr);
  connection->connect();
  connection->noDelay(true);
  connection->close(ConnectionCloseType::NoFlush);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(TcpClientConnectionImplTest, BadConnectConnRefused) {
  // Connecting to an invalid port on localhost will cause ECONNREFUSED which is a different code
  // path from other errors. Test this also.
  ClientConnectionPtr connection = dispatcher_->createClientConnection(
      Utility::resolveUrl(
          fmt::format("tcp://{}:1", Network::Test::getLoopbackAddressUrlString(GetParam()))),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr);
  connection->connect();
  connection->noDelay(true);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

class PipeClientConnectionImplTest : public testing::Test {
protected:
  PipeClientConnectionImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  const std::string path_{TestEnvironment::unixDomainSocketPath("foo")};
};

// Validate we skip setting socket options on UDS.
TEST_F(PipeClientConnectionImplTest, SkipSocketOptions) {
  auto option = std::make_shared<MockSocketOption>();
  EXPECT_CALL(*option, setOption(_, _)).Times(0);
  auto options = std::make_shared<Socket::Options>();
  options->emplace_back(option);
  ClientConnectionPtr connection = dispatcher_->createClientConnection(
      Utility::resolveUrl("unix://" + path_), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), options);
  connection->close(ConnectionCloseType::NoFlush);
}

// Validate we skip setting source address.
TEST_F(PipeClientConnectionImplTest, SkipSourceAddress) {
  ClientConnectionPtr connection = dispatcher_->createClientConnection(
      Utility::resolveUrl("unix://" + path_), Utility::resolveUrl("tcp://1.2.3.4:5"),
      Network::Test::createRawBufferSocket(), nullptr);
  connection->close(ConnectionCloseType::NoFlush);
}

} // namespace
} // namespace Network
} // namespace Envoy
