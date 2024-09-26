#include <cstdint>
#include <memory>
#include <string>

#include "envoy/common/platform.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/event/scaled_range_timer_manager.h"
#include "envoy/network/address.h"
#include "envoy/network/listener.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/empty_string.h"
#include "source/common/common/fmt.h"
#include "source/common/common/utility.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/tcp_listener_impl.h"
#include "source/common/network/utility.h"
#include "source/common/runtime/runtime_impl.h"

#include "test/common/memory/memory_test_utility.h"
#include "test/mocks/api/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/threadsafe_singleton_injector.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::EndsWith;
using testing::Eq;
using testing::HasSubstr;
using testing::InSequence;
using testing::Invoke;
using testing::InvokeWithoutArgs;
using testing::Optional;
using testing::Return;
using testing::SaveArg;
using testing::Sequence;
using testing::StartsWith;
using testing::StrictMock;

namespace Envoy {
namespace Network {
namespace {

class MockInternalListenerManager : public InternalListenerManager {
public:
  MOCK_METHOD(InternalListenerOptRef, findByAddress, (const Address::InstanceConstSharedPtr&), ());
};

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
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>();
  StreamInfo::StreamInfoImpl stream_info(dispatcher->timeSource(), nullptr,
                                         StreamInfo::FilterState::LifeSpan::Connection);
  EXPECT_ENVOY_BUG(
      ConnectionImpl(*dispatcher,
                     std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
                     Network::Test::createRawBufferSocket(), stream_info, false),
      "Client socket failure");
}

class TestClientConnectionImpl : public Network::ClientConnectionImpl {
public:
  using ClientConnectionImpl::ClientConnectionImpl;
  Buffer::Instance& readBuffer() { return *read_buffer_; }
};

class ConnectionImplTestBase {
protected:
  ConnectionImplTestBase()
      : api_(Api::createApiForTest(time_system_)),
        stream_info_(time_system_, nullptr, StreamInfo::FilterState::LifeSpan::Connection) {}

  virtual ~ConnectionImplTestBase() {
    EXPECT_TRUE(timer_destroyed_ || timer_ == nullptr);
    if (!timer_destroyed_) {
      delete timer_;
    }
  }

  virtual TransportSocketPtr createTransportSocket() {
    return Network::Test::createRawBufferSocket();
  }

  void setUpBasicConnectionWithAddress(const Address::InstanceConstSharedPtr& address) {
    if (dispatcher_ == nullptr) {
      dispatcher_ = api_->allocateDispatcher("test_thread");
    }
    socket_ = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(address);
    NiceMock<Network::MockListenerConfig> listener_config;
    Server::ThreadLocalOverloadStateOptRef overload_state;
    listener_ = std::make_unique<Network::TcpListenerImpl>(
        *dispatcher_, api_->randomGenerator(), runtime_, socket_, listener_callbacks_,
        listener_config.bindToPort(), listener_config.ignoreGlobalConnLimit(),
        listener_config.shouldBypassOverloadManager(),
        listener_config.maxConnectionsToAcceptPerSocketEvent(), overload_state);
    client_connection_ = std::make_unique<Network::TestClientConnectionImpl>(
        *dispatcher_, socket_->connectionInfoProvider().localAddress(), source_address_,
        createTransportSocket(), socket_options_, transport_socket_options_);
    client_connection_->addConnectionCallbacks(client_callbacks_);
    EXPECT_EQ(nullptr, client_connection_->ssl());
    const Network::ClientConnection& const_connection = *client_connection_;
    EXPECT_EQ(nullptr, const_connection.ssl());
    EXPECT_FALSE(client_connection_->connectionInfoProvider().localAddressRestored());
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
    EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));
    EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected))
        .WillOnce(InvokeWithoutArgs([&]() -> void {
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
          .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
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
    EXPECT_CALL(*factory, createBuffer_(_, _, _))
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
    EXPECT_CALL(dispatcher->buffer_factory_, createBuffer_(_, _, _))
        .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                  std::function<void()> above_overflow) -> Buffer::Instance* {
          // ConnectionImpl calls Envoy::MockBufferFactory::create(), which calls createBuffer_()
          // and wraps the returned raw pointer below with a unique_ptr.
          return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
        }));

    if (create_timer) {
      // This timer will be returned (transferring ownership) to the ConnectionImpl when
      // createTimer() is called to allocate the delayed close timer.
      timer_ = new Event::MockTimer(dispatcher.get());
      timer_->timer_destroyed_ = &timer_destroyed_;
    }

    NiceMock<Event::MockFileEvent>* file_event = new NiceMock<Event::MockFileEvent>;
    EXPECT_CALL(*dispatcher, createFileEvent_(0, _, _, _))
        .WillOnce(DoAll(SaveArg<1>(&file_ready_cb_), Return(file_event)));

    auto transport_socket = std::make_unique<NiceMock<MockTransportSocket>>();
    EXPECT_CALL(*transport_socket, canFlushClose()).WillRepeatedly(Return(true));

    return ConnectionMocks{std::move(dispatcher), timer_, std::move(transport_socket), file_event,
                           &file_ready_cb_};
  }
  Network::TestClientConnectionImpl* testClientConnection() const {
    return dynamic_cast<Network::TestClientConnectionImpl*>(client_connection_.get());
  }

  Event::MockTimer* timer_{nullptr};
  bool timer_destroyed_{false};
  Event::FileReadyCb file_ready_cb_;
  Event::SimulatedTimeSystem time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<Network::TcpListenSocket> socket_{nullptr};
  Network::MockTcpListenerCallbacks listener_callbacks_;
  NiceMock<Runtime::MockLoader> runtime_;
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
  Network::TransportSocketOptionsConstSharedPtr transport_socket_options_ = nullptr;
};

class ConnectionImplTest : public ConnectionImplTestBase,
                           public testing::TestWithParam<Address::IpVersion> {
protected:
  void setUpBasicConnection() {
    setUpBasicConnectionWithAddress(Network::Test::getCanonicalLoopbackAddress(GetParam()));
  }
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

TEST_P(ConnectionImplTest, SetSslConnection) {
  setUpBasicConnection();
  const Ssl::ConnectionInfoConstSharedPtr ssl_info = std::make_shared<Ssl::MockConnectionInfo>();
  client_connection_->connectionInfoSetter().setSslConnection(ssl_info);
  EXPECT_EQ(ssl_info, client_connection_->ssl());
  EXPECT_EQ(ssl_info, client_connection_->connectionInfoProvider().sslConnection());
  disconnect(false);
}

TEST_P(ConnectionImplTest, GetCongestionWindow) {
  setUpBasicConnection();
  connect();

// Congestion window is available on Posix(guarded by TCP_INFO) and Windows(guarded by
// SIO_TCP_INFO).
#if defined(TCP_INFO) || defined(SIO_TCP_INFO)
  EXPECT_GT(client_connection_->congestionWindowInBytes().value(), 500);
  EXPECT_GT(server_connection_->congestionWindowInBytes().value(), 500);
#else
  EXPECT_FALSE(client_connection_->congestionWindowInBytes().has_value());
  EXPECT_FALSE(server_connection_->congestionWindowInBytes().has_value());
#endif

  disconnect(true);
}

TEST_P(ConnectionImplTest, CloseDuringConnectCallback) {
  setUpBasicConnection();

  Buffer::OwnedImpl buffer("hello world");
  client_connection_->write(buffer, false);
  client_connection_->connect();
  EXPECT_TRUE(client_connection_->connecting());

  StrictMock<MockConnectionCallbacks> added_and_removed_callbacks;
  // Make sure removed callbacks don't get events.
  client_connection_->addConnectionCallbacks(added_and_removed_callbacks);
  client_connection_->removeConnectionCallbacks(added_and_removed_callbacks);

  // Make sure later callbacks don't receive a connected event if an earlier callback closed
  // the connection during its callback.
  StrictMock<MockConnectionCallbacks> later_callbacks;
  client_connection_->addConnectionCallbacks(later_callbacks);

  std::shared_ptr<MockReadFilter> add_and_remove_filter =
      std::make_shared<StrictMock<MockReadFilter>>();

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected))
      .WillOnce(InvokeWithoutArgs(
          [&]() -> void { client_connection_->close(ConnectionCloseType::NoFlush); }));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(later_callbacks, onEvent(ConnectionEvent::LocalClose));

  read_filter_ = std::make_shared<NiceMock<MockReadFilter>>();

  EXPECT_CALL(listener_callbacks_, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection_ = dispatcher_->createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket(), stream_info_);
        server_connection_->addConnectionCallbacks(server_callbacks_);
        server_connection_->addReadFilter(read_filter_);
        server_connection_->addReadFilter(add_and_remove_filter);
        server_connection_->removeReadFilter(add_and_remove_filter);
      }));
  EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(ConnectionImplTest, UnregisterRegisterDuringConnectCallback) {
  setUpBasicConnection();

  NiceMock<Network::MockConnectionCallbacks> upstream_callbacks_;
  // Verify the code path in the mixed connection pool, where the original
  // network callback is unregistered when Connected is raised, and a new
  // callback is registered.
  // event.
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
  EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        expected_callbacks--;
        // Register the new callback. It should immediately get the Connected
        // event without an extra dispatch loop.
        EXPECT_CALL(upstream_callbacks_, onEvent(ConnectionEvent::Connected));
        client_connection_->addConnectionCallbacks(upstream_callbacks_);
        // Remove the old connection callbacks, to regression test removal
        // under the stack of onEvent.
        client_connection_->removeConnectionCallbacks(client_callbacks_);
        if (expected_callbacks == 0) {
          dispatcher_->exit();
        }
      }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Swap the callbacks back as disconnect() expects client_callbacks_ to be
  // registered.
  client_connection_->removeConnectionCallbacks(upstream_callbacks_);
  client_connection_->addConnectionCallbacks(client_callbacks_);
  disconnect(true);
}

TEST_P(ConnectionImplTest, ImmediateConnectError) {
  dispatcher_ = api_->allocateDispatcher("test_thread");

  // Using a broadcast/multicast address as the connection destinations address causes an
  // immediate error return from connect().
  Address::InstanceConstSharedPtr broadcast_address;
  socket_ = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(GetParam()));
  if (socket_->connectionInfoProvider().localAddress()->ip()->version() == Address::IpVersion::v4) {
    broadcast_address = std::make_shared<Address::Ipv4Instance>("224.0.0.1", 0);
  } else {
    broadcast_address = std::make_shared<Address::Ipv6Instance>("ff02::1", 0);
  }

  client_connection_ = dispatcher_->createClientConnection(
      broadcast_address, source_address_, Network::Test::createRawBufferSocket(), nullptr, nullptr);
  client_connection_->addConnectionCallbacks(client_callbacks_);
  client_connection_->connect();

  // Verify that also the immediate connect errors generate a remote close event.
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_THAT(client_connection_->transportFailureReason(), StartsWith("immediate connect error"));
}

TEST_P(ConnectionImplTest, SetServerTransportSocketTimeout) {
  ConnectionMocks mocks = createConnectionMocks(false);
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);

  auto* mock_timer = new NiceMock<Event::MockTimer>();
  EXPECT_CALL(*mocks.dispatcher_,
              createScaledTypedTimer_(Event::ScaledTimerType::TransportSocketConnectTimeout, _))
      .WillOnce(DoAll(SaveArg<1>(&mock_timer->callback_), Return(mock_timer)));
  auto server_connection = std::make_unique<Network::ServerConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_);

  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(3 * 1000), _));
  Stats::MockCounter timeout_counter;
  EXPECT_CALL(timeout_counter, inc());
  server_connection->setTransportSocketConnectTimeout(std::chrono::seconds(3), timeout_counter);
  EXPECT_CALL(*transport_socket, closeSocket(ConnectionEvent::LocalClose));
  mock_timer->invokeCallback();
  EXPECT_THAT(stream_info_.connectionTerminationDetails(),
              Optional(HasSubstr("transport socket timeout")));
  EXPECT_EQ(server_connection->transportFailureReason(), "connect timeout");
}

TEST_P(ConnectionImplTest, SetServerTransportSocketTimeoutAfterConnect) {
  ConnectionMocks mocks = createConnectionMocks(false);
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);

  auto server_connection = std::make_unique<Network::ServerConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_);

  transport_socket->callbacks_->raiseEvent(ConnectionEvent::Connected);
  // This should be a no-op. No timer should be created.
  EXPECT_CALL(*mocks.dispatcher_, createTimer_(_)).Times(0);
  Stats::MockCounter timeout_counter;
  EXPECT_CALL(timeout_counter, inc()).Times(0);
  server_connection->setTransportSocketConnectTimeout(std::chrono::seconds(3), timeout_counter);

  server_connection->close(ConnectionCloseType::NoFlush);
}

TEST_P(ConnectionImplTest, ServerTransportSocketTimeoutDisabledOnConnect) {
  ConnectionMocks mocks = createConnectionMocks(false);
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);

  auto* mock_timer = new NiceMock<Event::MockTimer>();
  EXPECT_CALL(*mocks.dispatcher_,
              createScaledTypedTimer_(Event::ScaledTimerType::TransportSocketConnectTimeout, _))
      .WillOnce(DoAll(SaveArg<1>(&mock_timer->callback_), Return(mock_timer)));
  auto server_connection = std::make_unique<Network::ServerConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_);

  bool timer_destroyed = false;
  mock_timer->timer_destroyed_ = &timer_destroyed;
  EXPECT_CALL(*mock_timer, enableTimer(std::chrono::milliseconds(3 * 1000), _));

  Stats::MockCounter timeout_counter;
  EXPECT_CALL(timeout_counter, inc()).Times(0);
  server_connection->setTransportSocketConnectTimeout(std::chrono::seconds(3), timeout_counter);

  transport_socket->callbacks_->raiseEvent(ConnectionEvent::Connected);
  EXPECT_TRUE(timer_destroyed);
  if (!timer_destroyed) {
    delete mock_timer;
  }
  server_connection->close(ConnectionCloseType::NoFlush);
}

TEST_P(ConnectionImplTest, SocketOptions) {
  Network::ClientConnectionPtr upstream_connection_;

  setUpBasicConnection();

  Buffer::OwnedImpl buffer("hello world");
  client_connection_->write(buffer, false);
  client_connection_->connect();

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected))
      .WillOnce(InvokeWithoutArgs(
          [&]() -> void { client_connection_->close(ConnectionCloseType::NoFlush); }));
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
            socket_->connectionInfoProvider().localAddress(), source_address_,
            Network::Test::createRawBufferSocket(), server_connection_->socketOptions(), nullptr);
      }));
  EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
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
      .WillOnce(InvokeWithoutArgs(
          [&]() -> void { client_connection_->close(ConnectionCloseType::NoFlush); }));
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
            socket_->connectionInfoProvider().localAddress(), source_address_,
            Network::Test::createRawBufferSocket(), server_connection_->socketOptions(), nullptr);
        upstream_connection_->addConnectionCallbacks(upstream_callbacks_);
      }));
  EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(upstream_callbacks_, onEvent(ConnectionEvent::LocalClose));

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        upstream_connection_->close(ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

#if ENVOY_PLATFORM_ENABLE_SEND_RST
// Test that connection is AbortReset closed during callback.
TEST_P(ConnectionImplTest, ClientAbortResetDuringCallback) {
  Network::ClientConnectionPtr upstream_connection_;
  StrictMock<Network::MockConnectionCallbacks> upstream_callbacks_;
  setUpBasicConnection();

  Buffer::OwnedImpl buffer("hello world");
  client_connection_->write(buffer, false);
  client_connection_->connect();

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected))
      .WillOnce(InvokeWithoutArgs(
          [&]() -> void { client_connection_->close(ConnectionCloseType::AbortReset); }));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(client_connection_->detectedCloseType(), DetectedCloseType::LocalReset);
      }));

  read_filter_ = std::make_shared<NiceMock<MockReadFilter>>();

  EXPECT_CALL(listener_callbacks_, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection_ = dispatcher_->createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket(), stream_info_);
        server_connection_->addConnectionCallbacks(server_callbacks_);
        server_connection_->addReadFilter(read_filter_);

        upstream_connection_ = dispatcher_->createClientConnection(
            socket_->connectionInfoProvider().localAddress(), source_address_,
            Network::Test::createRawBufferSocket(), server_connection_->socketOptions(), nullptr);
        upstream_connection_->addConnectionCallbacks(upstream_callbacks_);
      }));
  EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));

  EXPECT_CALL(upstream_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(upstream_connection_->detectedCloseType(), DetectedCloseType::LocalReset);
      }));

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(server_connection_->detectedCloseType(), DetectedCloseType::RemoteReset);
        upstream_connection_->close(ConnectionCloseType::AbortReset);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test the connection is AbortReset closed and RST is detected for
// normal close event chain.
TEST_P(ConnectionImplTest, ClientAbortResetAfterCallback) {
  Network::ClientConnectionPtr upstream_connection_;
  StrictMock<Network::MockConnectionCallbacks> upstream_callbacks_;

  setUpBasicConnection();

  Buffer::OwnedImpl buffer("hello world");
  client_connection_->write(buffer, false);
  client_connection_->connect();

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::Connected))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  ;

  read_filter_ = std::make_shared<NiceMock<MockReadFilter>>();

  EXPECT_CALL(listener_callbacks_, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        server_connection_ = dispatcher_->createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket(), stream_info_);
        server_connection_->addConnectionCallbacks(server_callbacks_);
        server_connection_->addReadFilter(read_filter_);

        upstream_connection_ = dispatcher_->createClientConnection(
            socket_->connectionInfoProvider().localAddress(), source_address_,
            Network::Test::createRawBufferSocket(), server_connection_->socketOptions(), nullptr);
        upstream_connection_->addConnectionCallbacks(upstream_callbacks_);
      }));

  EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(client_connection_->detectedCloseType(), DetectedCloseType::LocalReset);
      }));
  client_connection_->close(ConnectionCloseType::AbortReset);

  EXPECT_CALL(upstream_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(upstream_connection_->detectedCloseType(), DetectedCloseType::LocalReset);
      }));

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(server_connection_->detectedCloseType(), DetectedCloseType::RemoteReset);
        upstream_connection_->close(ConnectionCloseType::AbortReset);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test the server connection is AbortReset closed and RST is detected
// from the client.
TEST_P(ConnectionImplTest, ServerResetClose) {
  setUpBasicConnection();
  connect();

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(client_connection_->detectedCloseType(), DetectedCloseType::RemoteReset);
        dispatcher_->exit();
      }));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(server_connection_->detectedCloseType(), DetectedCloseType::LocalReset);
      }));

  server_connection_->close(ConnectionCloseType::AbortReset);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}
#endif

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
  EXPECT_TRUE(client_connection_->connecting());
  client_connection_->connect();
  // The Network::Connection class oddly uses onWrite as its indicator of if
  // it's done connection, rather than the Connected event.
  EXPECT_TRUE(client_connection_->connecting());

  std::shared_ptr<MockWriteFilter> write_filter(new MockWriteFilter());
  std::shared_ptr<MockFilter> filter(new MockFilter());
  client_connection_->addFilter(filter);
  client_connection_->addWriteFilter(write_filter);

  // Make sure removed filters don't get callbacks.
  std::shared_ptr<MockReadFilter> read_filter(new StrictMock<MockReadFilter>());
  client_connection_->addReadFilter(read_filter);
  client_connection_->removeReadFilter(read_filter);

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
  EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));

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
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));

  Buffer::OwnedImpl data("1234");
  client_connection_->write(data, false);
  client_connection_->write(data, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Ensure the new counter logic in ReadDisable avoids tripping asserts in ReadDisable guarding
// against actual enabling twice in a row.
TEST_P(ConnectionImplTest, ReadDisable) {
  ConnectionMocks mocks = createConnectionMocks(false);
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);
  auto connection = std::make_unique<Network::ConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_, true);

  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            connection->readDisable(true));
  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            connection->readDisable(false));

  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            connection->readDisable(true));
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection->readDisable(true));
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection->readDisable(false));
  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            connection->readDisable(false));

  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            connection->readDisable(true));
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection->readDisable(true));
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection->readDisable(false));
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection->readDisable(true));
  EXPECT_CALL(*mocks.file_event_, setEnabled(_)).Times(0);
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection->readDisable(false));
  EXPECT_CALL(*mocks.file_event_, setEnabled(_));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            connection->readDisable(false));

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
    EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
              client_connection_->readDisable(true));
    EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("data"), false))
        .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> FilterStatus {
          buffer.drain(buffer.length());
          dispatcher_->exit();
          return FilterStatus::StopIteration;
        }));
    EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
              client_connection_->readDisable(false));
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
    EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
              client_connection_->readDisable(true));
    EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
              client_connection_->readDisable(false)); // Sets dispatch_buffered_data_
    EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
              client_connection_->readDisable(true));
    EXPECT_CALL(*client_read_filter, onData(_, _)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Now drain the connection's buffer and try to do a read which should _not_
  // pass up the stack (no data is read)
  {
    connection_buffer->drain(connection_buffer->length());
    EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
              client_connection_->readDisable(false));
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

  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            client_connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            client_connection_->readDisable(false));

  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            client_connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled,
            client_connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled,
            client_connection_->readDisable(false));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            client_connection_->readDisable(false));

  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            client_connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled,
            client_connection_->readDisable(true));
  disconnect(false);
#ifndef NDEBUG
  // When running in debug mode, verify that calls to readDisable and readEnabled on a closed socket
  // trigger ASSERT failures.
  EXPECT_DEBUG_DEATH(client_connection_->readEnabled(), "");
  EXPECT_DEBUG_DEATH(client_connection_->readDisable(true), "");
  EXPECT_DEBUG_DEATH(client_connection_->readDisable(false), "");
#else
  // When running in release mode, verify that calls to readDisable change the readEnabled state.
  EXPECT_EQ(Connection::ReadDisableStatus::NoTransition, client_connection_->readDisable(false));
  EXPECT_EQ(Connection::ReadDisableStatus::NoTransition, client_connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::NoTransition, client_connection_->readDisable(false));
  EXPECT_FALSE(client_connection_->readEnabled());
  EXPECT_EQ(Connection::ReadDisableStatus::NoTransition, client_connection_->readDisable(false));
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

  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            client_connection_->readDisable(true));

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
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            client_connection_->readDisable(true));

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose)).Times(0);
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  server_connection_->close(ConnectionCloseType::FlushWrite);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            client_connection_->readDisable(false));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

#if ENVOY_PLATFORM_ENABLE_SEND_RST
// Test normal RST close without readDisable.
TEST_P(ConnectionImplTest, RstCloseOnNotReadDisabledConnection) {
  setUpBasicConnection();
  connect();

  // Connection is not readDisabled, and detect_early_close_ is true.
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(client_connection_->detectedCloseType(), DetectedCloseType::RemoteReset);
        dispatcher_->exit();
      }));

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(server_connection_->detectedCloseType(), DetectedCloseType::LocalReset);
      }));
  server_connection_->close(ConnectionCloseType::AbortReset);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test normal RST close with readDisable=true.
TEST_P(ConnectionImplTest, RstCloseOnReadDisabledConnectionThenWrite) {
  setUpBasicConnection();
  connect();

  // Connection is readDisabled, and detect_early_close_ is true.
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            client_connection_->readDisable(true));

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose)).Times(0);
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(server_connection_->detectedCloseType(), DetectedCloseType::LocalReset);
      }));
  server_connection_->close(ConnectionCloseType::AbortReset);

  // The reset error is triggered by write event.
  // write error: Connection reset by peer, code: 9
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(client_connection_->detectedCloseType(), DetectedCloseType::RemoteReset);
        dispatcher_->exit();
      }));
  Buffer::OwnedImpl buffer("data");
  client_connection_->write(buffer, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test reset close with readDisable=true. detect_early_close_ will not
// impact the read and write behaviour for RST.
TEST_P(ConnectionImplTest, RstCloseOnReadEarlyCloseDisabledThenWrite) {
  setUpBasicConnection();
  connect();

  // Connection is readDisabled, and detect_early_close_ is false.
  client_connection_->detectEarlyCloseWhenReadDisabled(false);
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            client_connection_->readDisable(true));

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose)).Times(0);
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(server_connection_->detectedCloseType(), DetectedCloseType::LocalReset);
      }));
  server_connection_->close(ConnectionCloseType::AbortReset);

  // The reset error is triggered by write event.
  // write error: Connection reset by peer, code: 9
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(client_connection_->detectedCloseType(), DetectedCloseType::RemoteReset);
        dispatcher_->exit();
      }));
  Buffer::OwnedImpl buffer("data");
  client_connection_->write(buffer, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}
#endif

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

#if ENVOY_PLATFORM_ENABLE_SEND_RST
// Test that connection is immediately closed when RST is detected even
// half-close is enabled
TEST_P(ConnectionImplTest, HalfCloseResetClose) {
  setUpBasicConnection();
  connect();

  server_connection_->enableHalfClose(true);
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(server_connection_->detectedCloseType(), DetectedCloseType::RemoteReset);
        dispatcher_->exit();
      }));
  EXPECT_CALL(*read_filter_, onData(_, _)).Times(0);
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(client_connection_->detectedCloseType(), DetectedCloseType::LocalReset);
        dispatcher_->exit();
      }));
  client_connection_->close(ConnectionCloseType::AbortReset);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that no remote close event will be propagated back to peer, when a connection is
// half-closed and then the connection is normally closed. This is the current behavior.
TEST_P(ConnectionImplTest, HalfCloseThenNormallClose) {
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
  // Client is half closed at first.
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

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));

  // After the half closed from one way, no event will be propagate back to server connection.
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose)).Times(0);
  // Then client is going to normally close the connection.
  client_connection_->close(ConnectionCloseType::NoFlush);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  server_connection_->write(empty_buffer, true);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that no remote close event will be propagated back to peer, when a connection is
// half-closed and then the connection is RST closed. This is same as other close type.
TEST_P(ConnectionImplTest, HalfCloseThenResetClose) {
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
  // Client is half closed at first.
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

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(client_connection_->detectedCloseType(), DetectedCloseType::LocalReset);
        dispatcher_->exit();
      }));

  // After the half closed from one way, no event will be propagate back to server connection.
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose)).Times(0);
  // Then client is going to reset the connection.
  client_connection_->close(ConnectionCloseType::AbortReset);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  server_connection_->write(empty_buffer, true);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that no remote close event will be propagated back to peer, when a connection is
// half-closed and then the connection is RST closed. Writing data to the half closed and then
// reset connection will lead to broken pipe error rather than reset error.
// This behavior is different for windows.
TEST_P(ConnectionImplTest, HalfCloseThenResetCloseThenWriteData) {
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
  // Client is half closed at first.
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

  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(client_connection_->detectedCloseType(), DetectedCloseType::LocalReset);
        dispatcher_->exit();
      }));

  // After the half closed from one way, no event will be propagate back to server connection.
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose)).Times(0);
  // Then client is going to reset the connection.
  client_connection_->close(ConnectionCloseType::AbortReset);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Write error: Broken pipe, code: 12 rather than the reset error.
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void {
        EXPECT_EQ(server_connection_->detectedCloseType(), DetectedCloseType::Normal);
        dispatcher_->exit();
      }));
  // Tring to write data to the closed connection.
  Buffer::OwnedImpl server_buffer("data");
  server_connection_->write(server_buffer, false);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}
#endif

// Test that connections do not detect early close when half-close is enabled
TEST_P(ConnectionImplTest, HalfCloseNoEarlyCloseDetection) {
  setUpBasicConnection();
  connect();

  server_connection_->enableHalfClose(true);
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            server_connection_->readDisable(true));

  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose)).Times(0);
  EXPECT_CALL(*read_filter_, onData(_, _)).Times(0);
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose))
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  client_connection_->close(ConnectionCloseType::FlushWrite);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            server_connection_->readDisable(false));
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

  StrictMock<MockConnectionCallbacks> added_and_removed_callbacks;
  // Make sure removed connections don't get events.
  client_connection_->addConnectionCallbacks(added_and_removed_callbacks);
  client_connection_->removeConnectionCallbacks(added_and_removed_callbacks);

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
  EXPECT_FALSE(testClientConnection()->shouldDrainReadBuffer());
  EXPECT_TRUE(client_connection_->readEnabled());
  // Add 2 bytes to the buffer so that it sits at exactly the read limit. Verify that
  // shouldDrainReadBuffer is true, but the connection remains read enabled.
  {
    Buffer::OwnedImpl buffer("12");
    server_connection_->write(buffer, false);
    EXPECT_CALL(*client_read_filter, onData(_, false)).WillOnce(Invoke(on_filter_data_exit));
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_TRUE(testClientConnection()->shouldDrainReadBuffer());
    EXPECT_FALSE(testClientConnection()->readBuffer().highWatermarkTriggered());
    EXPECT_TRUE(client_connection_->readEnabled());
  }
  // Add 1 bytes to the buffer to go over the high watermark. Verify the connection becomes read
  // disabled.
  {
    Buffer::OwnedImpl buffer("3");
    server_connection_->write(buffer, false);
    EXPECT_CALL(*client_read_filter, onData(_, false)).WillOnce(Invoke(on_filter_data_exit));
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_TRUE(testClientConnection()->readBuffer().highWatermarkTriggered());
    EXPECT_TRUE(testClientConnection()->shouldDrainReadBuffer());
    EXPECT_FALSE(client_connection_->readEnabled());
  }

  // Drain 2 bytes from the buffer. This bring sit below the low watermark, and
  // read enables, as well as triggering a kick for the remaining byte.
  {
    testClientConnection()->readBuffer().drain(2);
    EXPECT_FALSE(testClientConnection()->readBuffer().highWatermarkTriggered());
    EXPECT_FALSE(testClientConnection()->shouldDrainReadBuffer());
    EXPECT_TRUE(client_connection_->readEnabled());

    EXPECT_CALL(*client_read_filter, onData(_, false));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Add 2 bytes to the buffer and verify the connection becomes read disabled
  // again.
  {
    Buffer::OwnedImpl buffer("45");
    server_connection_->write(buffer, false);
    EXPECT_CALL(*client_read_filter, onData(_, false)).WillOnce(Invoke(on_filter_data_exit));
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_TRUE(testClientConnection()->readBuffer().highWatermarkTriggered());
    EXPECT_TRUE(testClientConnection()->shouldDrainReadBuffer());
    EXPECT_FALSE(client_connection_->readEnabled());
  }

  // Now have the consumer read disable.
  // This time when the buffer is drained, there will be no kick as the consumer
  // does not want to read.
  {
    EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled,
              client_connection_->readDisable(true));
    testClientConnection()->readBuffer().drain(2);
    EXPECT_FALSE(testClientConnection()->readBuffer().highWatermarkTriggered());
    EXPECT_FALSE(testClientConnection()->shouldDrainReadBuffer());
    EXPECT_FALSE(client_connection_->readEnabled());

    EXPECT_CALL(*client_read_filter, onData(_, false)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Now read enable again.
  // Inside the onData call, readDisable and readEnable. This should trigger
  // another kick on the next dispatcher loop, so onData gets called twice.
  {
    EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
              client_connection_->readDisable(false));
    EXPECT_CALL(*client_read_filter, onData(_, false))
        .Times(2)
        .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
          EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
                    client_connection_->readDisable(true));
          EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
                    client_connection_->readDisable(false));
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
    EXPECT_TRUE(testClientConnection()->shouldDrainReadBuffer());
    EXPECT_FALSE(client_connection_->readEnabled());

    // Read disable and read enable, to set dispatch_buffered_data_ true.
    EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled,
              client_connection_->readDisable(true));
    EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled,
              client_connection_->readDisable(false));
    // Now event loop. This hits the early on-Read path. As above, read
    // disable and read enable from inside the stack of onData, to ensure that
    // dispatch_buffered_data_ works correctly.
    EXPECT_CALL(*client_read_filter, onData(_, false))
        .Times(2)
        .WillOnce(Invoke([&](Buffer::Instance&, bool) -> FilterStatus {
          EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled,
                    client_connection_->readDisable(true));
          EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled,
                    client_connection_->readDisable(false));
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
  auto os_calls =
      std::make_unique<TestThreadsafeSingletonInjector<Api::OsSysCallsImpl>>(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .WillRepeatedly(Invoke([&](os_fd_t, const iovec*, int) -> Api::SysCallSizeResult {
        return {-1, SOCKET_ERROR_AGAIN};
      }));

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .WillRepeatedly(Invoke([&](os_fd_t, void*, size_t, int) -> Api::SysCallSizeResult {
        return {-1, SOCKET_ERROR_AGAIN};
      }));

  EXPECT_CALL(os_sys_calls, send(_, _, _, _))
      .WillOnce(Invoke([&](os_fd_t, void*, size_t, int) -> Api::SysCallSizeResult {
        dispatcher_->exit();
        // Return to default os_sys_calls implementation
        os_calls.reset();
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
  EXPECT_CALL(client_callbacks_, onBelowWriteBufferLowWatermark());

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
    if (bytes_to_write + bytes_buffered > 10 && is_below) {
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
    EXPECT_CALL(os_sys_calls, send(_, _, _, _))
        .WillOnce(Invoke([&](os_fd_t, void*, size_t, int) -> Api::SysCallSizeResult {
          client_write_buffer_->drain(bytes_to_flush);
          dispatcher_->exit();
          return {-1, SOCKET_ERROR_AGAIN};
        }))
        .WillRepeatedly(Invoke([&](os_fd_t, void*, size_t, int) -> Api::SysCallSizeResult {
          return {-1, SOCKET_ERROR_AGAIN};
        }));

    client_connection_->write(buffer_to_write, false);
    dispatcher_->run(Event::Dispatcher::RunType::Block);
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
  EXPECT_EQ(address_string,
            server_connection_->connectionInfoProvider().remoteAddress()->ip()->addressAsString());

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
        socket.connectionInfoProvider().setLocalAddress(new_source_address);
        return true;
      }));

  socket_options_ = std::make_shared<Socket::Options>();
  socket_options_->emplace_back(std::move(option));
  setUpBasicConnection();
  connect();
  EXPECT_EQ(address_string,
            server_connection_->connectionInfoProvider().remoteAddress()->ip()->addressAsString());

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
  socket_ = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
      Network::Test::getCanonicalLoopbackAddress(GetParam()));
  NiceMock<Network::MockListenerConfig> listener_config;
  Server::ThreadLocalOverloadStateOptRef overload_state;
  listener_ = std::make_unique<Network::TcpListenerImpl>(
      *dispatcher_, api_->randomGenerator(), runtime_, socket_, listener_callbacks_,
      listener_config.bindToPort(), listener_config.ignoreGlobalConnLimit(),
      listener_config.shouldBypassOverloadManager(),
      listener_config.maxConnectionsToAcceptPerSocketEvent(), overload_state);

  client_connection_ = dispatcher_->createClientConnection(
      socket_->connectionInfoProvider().localAddress(), source_address_,
      Network::Test::createRawBufferSocket(), nullptr, nullptr);

  MockConnectionStats connection_stats;
  client_connection_->setConnectionStats(connection_stats.toBufferStats());
  client_connection_->addConnectionCallbacks(client_callbacks_);
  EXPECT_CALL(connection_stats.bind_errors_, inc());
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_THAT(client_connection_->transportFailureReason(), StartsWith("failed to bind to"));
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
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));

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
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("data"), false))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&]() -> FilterStatus {
        time_system_.setMonotonicTime(std::chrono::milliseconds(50));
        dispatcher_->exit();
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose));
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
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
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
  EXPECT_CALL(stats.delayed_close_timeouts_, inc());
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
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
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(*client_read_filter, onData(BufferStringEqual("Connection: Close"), false))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&]() -> FilterStatus {
        time_system_.setMonotonicTime(std::chrono::milliseconds(100));
        return FilterStatus::StopIteration;
      }));
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
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
  EXPECT_CALL(server_callbacks_, onEvent(ConnectionEvent::LocalClose));
  server_connection_->close(ConnectionCloseType::FlushWrite);
  EXPECT_CALL(stats.delayed_close_timeouts_, inc()).Times(0);
  EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
      .Times(1)
      .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test that delayed close processing can be disabled by setting the delayed close timeout
// interval to 0.
TEST_P(ConnectionImplTest, FlushWriteAndDelayConfigDisabledTest) {
  InSequence s1;

  NiceMock<MockConnectionCallbacks> callbacks;
  NiceMock<Event::MockDispatcher> dispatcher;
  EXPECT_CALL(dispatcher.buffer_factory_, createBuffer_(_, _, _))
      .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                std::function<void()> above_overflow) -> Buffer::Instance* {
        return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
      }));
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);
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
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);
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

  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _));
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
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _));
  EXPECT_TRUE((*mocks.file_ready_cb_)(Event::FileReadyType::Write).ok());

  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual("ata"), _))
      .WillOnce(Invoke([&](Buffer::Instance& buffer, bool) -> IoResult {
        // Flush the entire buffer.
        uint64_t bytes_drained = buffer.length();
        buffer.drain(buffer.length());
        return IoResult{PostIoAction::KeepOpen, bytes_drained, false};
      }));
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _));
  EXPECT_TRUE((*mocks.file_ready_cb_)(Event::FileReadyType::Write).ok());

  // Force the delayed close timeout to trigger so the connection is cleaned up.
  mocks.timer_->invokeCallback();
}

// Test that the delayed close timer is not reset by spurious fd Write events that either consume 0
// bytes from the output buffer or are delivered after close(FlushWriteAndDelay).
TEST_P(ConnectionImplTest, IgnoreSpuriousFdWriteEventsDuringFlushWriteAndDelay) {
  ConnectionMocks mocks = createConnectionMocks();
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);
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

  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _));
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
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _));
  EXPECT_TRUE((*mocks.file_ready_cb_)(Event::FileReadyType::Write).ok());

  // Handle a write event and drain 0 bytes from the buffer. Verify that the timer is not reset.
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual("ata"), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> IoResult {
        // Don't consume any bytes.
        return IoResult{PostIoAction::KeepOpen, 0, false};
      }));
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _)).Times(0);
  EXPECT_TRUE((*mocks.file_ready_cb_)(Event::FileReadyType::Write).ok());

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
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _));
  EXPECT_TRUE((*mocks.file_ready_cb_)(Event::FileReadyType::Write).ok());

  // Handle a write event after entering the half-closed state. Verify that the timer is not reset
  // because write consumed 0 bytes from the empty buffer.
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual(""), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> IoResult {
        EXPECT_EQ(server_connection->state(), Connection::State::Closing);
        return IoResult{PostIoAction::KeepOpen, 0, false};
      }));
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _)).Times(0);
  EXPECT_TRUE((*mocks.file_ready_cb_)(Event::FileReadyType::Write).ok());

  // Handle a write event that somehow drains bytes from an empty output buffer. Since
  // some bytes were consumed, the timer is reset.
  EXPECT_CALL(*transport_socket, doWrite(BufferStringEqual(""), _))
      .WillOnce(Invoke([&](Buffer::Instance&, bool) -> IoResult {
        EXPECT_EQ(server_connection->state(), Connection::State::Closing);
        return IoResult{PostIoAction::KeepOpen, 1, false};
      }));
  EXPECT_CALL(*mocks.timer_, enableTimer(timeout, _));
  EXPECT_TRUE((*mocks.file_ready_cb_)(Event::FileReadyType::Write).ok());

  // Force the delayed close timeout to trigger so the connection is cleaned up.
  mocks.timer_->invokeCallback();
}

// Test that tearing down the connection will disable the delayed close timer.
TEST_P(ConnectionImplTest, DelayedCloseTimeoutDisableOnSocketClose) {
  ConnectionMocks mocks = createConnectionMocks();
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);
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
  EXPECT_CALL(*mocks.timer_, enableTimer(_, _));
  // Enable the delayed close timer.
  server_connection->close(ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_CALL(*mocks.timer_, disableTimer());
  // This close() will call closeSocket(), which should disable the timer to avoid triggering it
  // after the connection's data structures have been reset.
  server_connection->close(ConnectionCloseType::NoFlush);
}

// Test that the delayed close timeout callback is resilient to connection teardown edge cases.
TEST_P(ConnectionImplTest, DelayedCloseTimeoutNullStats) {
  ConnectionMocks mocks = createConnectionMocks();
  MockTransportSocket* transport_socket = mocks.transport_socket_.get();
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);
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

  EXPECT_CALL(*mocks.timer_, enableTimer(_, _));
  server_connection->close(ConnectionCloseType::FlushWriteAndDelay);
  EXPECT_CALL(*mocks.timer_, disableTimer());
  // The following close() will call closeSocket() and reset internal data structures such as
  // stats.
  server_connection->close(ConnectionCloseType::NoFlush);
}

// Test DumpState methods.
TEST_P(ConnectionImplTest, NetworkSocketDumpsWithoutAllocatingMemory) {
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);
  Address::InstanceConstSharedPtr server_addr;
  Address::InstanceConstSharedPtr local_addr;
  if (GetParam() == Network::Address::IpVersion::v4) {
    server_addr = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance("1.1.1.1", 80, nullptr)};
    local_addr = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv4Instance("1.2.3.4", 56789, nullptr)};
  } else {
    server_addr = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv6Instance("::1", 80, nullptr)};
    local_addr = Network::Address::InstanceConstSharedPtr{
        new Network::Address::Ipv6Instance("::1:2:3:4", 56789, nullptr)};
  }

  auto connection_socket =
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), local_addr, server_addr);
  connection_socket->setRequestedServerName("envoyproxy.io");

  // Start measuring memory and dump state.
  Memory::TestUtil::MemoryTest memory_test;
  connection_socket->dumpState(ostream, 0);
  EXPECT_EQ(memory_test.consumedBytes(), 0);

  // Check socket dump
  const auto contents = ostream.contents();
  EXPECT_THAT(contents, HasSubstr("ListenSocketImpl"));
  EXPECT_THAT(contents, HasSubstr("transport_protocol_: "));
  EXPECT_THAT(contents, HasSubstr("ConnectionInfoSetterImpl"));
  if (GetParam() == Network::Address::IpVersion::v4) {
    EXPECT_THAT(
        contents,
        HasSubstr(
            "remote_address_: 1.1.1.1:80, direct_remote_address_: 1.1.1.1:80, local_address_: "
            "1.2.3.4:56789, server_name_: envoyproxy.io"));
  } else {
    EXPECT_THAT(
        contents,
        HasSubstr("remote_address_: [::1]:80, direct_remote_address_: [::1]:80, local_address_: "
                  "[::1:2:3:4]:56789, server_name_: envoyproxy.io"));
  }
}

TEST_P(ConnectionImplTest, NetworkConnectionDumpsWithoutAllocatingMemory) {
  std::array<char, 1024> buffer;
  OutputBufferStream ostream{buffer.data(), buffer.size()};
  ConnectionMocks mocks = createConnectionMocks(false);
  IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);

  auto server_connection = std::make_unique<Network::ServerConnectionImpl>(
      *mocks.dispatcher_,
      std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
      std::move(mocks.transport_socket_), stream_info_);

  // Start measuring memory and dump state.
  Memory::TestUtil::MemoryTest memory_test;
  server_connection->dumpState(ostream, 0);
  EXPECT_EQ(memory_test.consumedBytes(), 0);

  // Check connection data
  EXPECT_THAT(ostream.contents(), HasSubstr("ConnectionImpl"));
  EXPECT_THAT(ostream.contents(),
              HasSubstr("connecting_: 0, bind_error_: 0, state(): Open, read_buffer_limit_: 0"));
  // Check socket starts dumping
  EXPECT_THAT(ostream.contents(), HasSubstr("ListenSocketImpl"));

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
  MockTransportConnectionImplTest()
      : stream_info_(dispatcher_.timeSource(), nullptr,
                     StreamInfo::FilterState::LifeSpan::Connection) {
    EXPECT_CALL(dispatcher_, isThreadSafe()).WillRepeatedly(Return(true));
    EXPECT_CALL(dispatcher_.buffer_factory_, createBuffer_(_, _, _))
        .WillRepeatedly(Invoke([](std::function<void()> below_low, std::function<void()> above_high,
                                  std::function<void()> above_overflow) -> Buffer::Instance* {
          return new Buffer::WatermarkBuffer(below_low, above_high, above_overflow);
        }));

    file_event_ = new NiceMock<Event::MockFileEvent>;
    EXPECT_CALL(dispatcher_, createFileEvent_(0, _, _, _))
        .WillOnce(DoAll(SaveArg<1>(&file_ready_cb_), Return(file_event_)));
    transport_socket_ = new NiceMock<MockTransportSocket>;
    EXPECT_CALL(*transport_socket_, setTransportSocketCallbacks(_))
        .WillOnce(Invoke([this](TransportSocketCallbacks& callbacks) {
          transport_socket_callbacks_ = &callbacks;
        }));
    IoHandlePtr io_handle = std::make_unique<Network::Test::IoSocketHandlePlatformImpl>(0);
    connection_ = std::make_unique<ConnectionImpl>(
        dispatcher_, std::make_unique<ConnectionSocketImpl>(std::move(io_handle), nullptr, nullptr),
        TransportSocketPtr(transport_socket_), stream_info_, true);
    connection_->addConnectionCallbacks(callbacks_);
    // File events will trigger setTrackedObject on the dispatcher.
    EXPECT_CALL(dispatcher_, pushTrackedObject(_)).Times(AnyNumber());
    EXPECT_CALL(dispatcher_, popTrackedObject(_)).Times(AnyNumber());
  }

  ~MockTransportConnectionImplTest() override { connection_->close(ConnectionCloseType::NoFlush); }

  // This may be invoked for doWrite() on the transport to simulate all the data
  // being written.
  static IoResult simulateSuccessfulWrite(Buffer::Instance& buffer, bool) {
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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
}

// Verify that read resumptions requested via setTransportSocketIsReadable() are scheduled once read
// is re-enabled.
TEST_F(MockTransportConnectionImplTest, ReadBufferReadyResumeAfterReadDisable) {
  InSequence s;

  std::shared_ptr<MockReadFilter> read_filter(new StrictMock<MockReadFilter>());
  connection_->enableHalfClose(true);
  connection_->addReadFilter(read_filter);

  EXPECT_CALL(*file_event_, setEnabled(Event::FileReadyType::Write));
  connection_->readDisable(true);
  EXPECT_CALL(*file_event_, setEnabled(Event::FileReadyType::Read | Event::FileReadyType::Write));
  // No calls to activate when re-enabling if there are no pending read requests.
  EXPECT_CALL(*file_event_, activate(_)).Times(0);
  connection_->readDisable(false);

  // setTransportSocketIsReadable triggers an immediate call to activate.
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Read));
  connection_->setTransportSocketIsReadable();

  // When processing a sequence of read disable/read enable, changes to the enabled event mask
  // happen only when the disable count transitions to/from 0.
  EXPECT_CALL(*file_event_, setEnabled(Event::FileReadyType::Write));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(false));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(false));
  EXPECT_CALL(*file_event_, setEnabled(Event::FileReadyType::Read | Event::FileReadyType::Write));
  // Expect a read activation since there have been no transport doRead calls since the call to
  // setTransportSocketIsReadable.
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Read));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            connection_->readDisable(false));

  // No calls to doRead when file_ready_cb is invoked while read disabled.
  EXPECT_CALL(*file_event_, setEnabled(_));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            connection_->readDisable(true));
  EXPECT_CALL(*transport_socket_, doRead(_)).Times(0);
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());

  // Expect a read activate when re-enabling since the file ready cb has not done a read.
  EXPECT_CALL(*file_event_, setEnabled(_));
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Read));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            connection_->readDisable(false));

  // Do a read to clear the transport_wants_read_ flag, verify that no read activation is scheduled.
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_CALL(*file_event_, setEnabled(_));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            connection_->readDisable(true));
  EXPECT_CALL(*file_event_, setEnabled(_));
  // No read activate call.
  EXPECT_CALL(*file_event_, activate(_)).Times(0);
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            connection_->readDisable(false));
}

// Verify that read resumption is scheduled when read is re-enabled while the read buffer is
// non-empty.
TEST_F(MockTransportConnectionImplTest, ReadBufferResumeAfterReadDisable) {
  InSequence s;

  std::shared_ptr<MockReadFilter> read_filter(new StrictMock<MockReadFilter>());
  connection_->setBufferLimits(5);
  connection_->enableHalfClose(true);
  connection_->addReadFilter(read_filter);

  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Invoke([](Buffer::Instance& buffer) -> IoResult {
        buffer.add("0123");
        return {PostIoAction::KeepOpen, 4, false};
      }));
  // Buffer is under the read limit, expect no changes to the file event registration.
  EXPECT_CALL(*file_event_, setEnabled(_)).Times(0);
  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*read_filter, onData(_, false)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_FALSE(connection_->shouldDrainReadBuffer());

  // Do a second read to hit the read limit.
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Invoke([](Buffer::Instance& buffer) -> IoResult {
        buffer.add("4");
        return {PostIoAction::KeepOpen, 1, false};
      }));
  // Buffer is exactly at the read limit, expect no changes to the file event registration.
  EXPECT_CALL(*file_event_, setEnabled(_)).Times(0);
  EXPECT_CALL(*read_filter, onData(_, false)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_TRUE(connection_->shouldDrainReadBuffer());

  // Do a third read to trigger the high watermark.
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Invoke([](Buffer::Instance& buffer) -> IoResult {
        buffer.add("5");
        return {PostIoAction::KeepOpen, 1, false};
      }));
  // Expect a change to the event mask when going over the read limit.
  EXPECT_CALL(*file_event_, setEnabled(Event::FileReadyType::Write));
  EXPECT_CALL(*read_filter, onData(_, false)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_TRUE(connection_->shouldDrainReadBuffer());

  // Already read disabled, expect no changes to enabled events mask.
  EXPECT_CALL(*file_event_, setEnabled(_)).Times(0);
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(false));
  // Read buffer is at the high watermark so read_disable_count should be == 1. Expect a read
  // activate but no call to setEnable to change the registration mask.
  EXPECT_CALL(*file_event_, setEnabled(_)).Times(0);
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Read));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(false));

  // Invoke the file event cb while read_disable_count_ == 1 to partially drain the read buffer.
  // Expect no transport reads.
  EXPECT_CALL(*transport_socket_, doRead(_)).Times(0);
  EXPECT_CALL(*read_filter, onData(_, _))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) -> FilterStatus {
        EXPECT_EQ(6, data.length());
        data.drain(data.length() - 1);
        return FilterStatus::Continue;
      }));
  // Partial drain of the read buffer below low watermark triggers an update to the fd enabled mask
  // and a read activate since the read buffer is not empty.
  EXPECT_CALL(*file_event_, setEnabled(Event::FileReadyType::Read | Event::FileReadyType::Write));
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Read));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_FALSE(connection_->shouldDrainReadBuffer());

  // Drain the rest of the buffer and verify there are no spurious read activate calls.
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  EXPECT_CALL(*read_filter, onData(_, _))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) -> FilterStatus {
        EXPECT_EQ(1, data.length());
        data.drain(1);
        return FilterStatus::Continue;
      }));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_FALSE(connection_->shouldDrainReadBuffer());

  EXPECT_CALL(*file_event_, setEnabled(_));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            connection_->readDisable(true));
  EXPECT_CALL(*file_event_, setEnabled(_));
  // read buffer is empty, no read activate call.
  EXPECT_CALL(*file_event_, activate(_)).Times(0);
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            connection_->readDisable(false));
}

// Verify that transport_wants_read_ read resumption is not lost when processing read buffer
// high-watermark resumptions.
TEST_F(MockTransportConnectionImplTest, ResumeWhileAndAfterReadDisable) {
  InSequence s;

  std::shared_ptr<MockReadFilter> read_filter(new StrictMock<MockReadFilter>());
  connection_->setBufferLimits(5);
  connection_->enableHalfClose(true);
  connection_->addReadFilter(read_filter);

  // Add some data to the read buffer and also call setTransportSocketIsReadable to mimic what
  // transport sockets are expected to do when the read buffer high watermark is hit.
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Invoke([this](Buffer::Instance& buffer) -> IoResult {
        buffer.add("0123456789");
        connection_->setTransportSocketIsReadable();
        return {PostIoAction::KeepOpen, 10, false};
      }));
  // Expect a change to the event mask when hitting the read buffer high-watermark.
  EXPECT_CALL(*file_event_, setEnabled(Event::FileReadyType::Write));
  // The setTransportSocketIsReadable does not call activate because read_disable_count_ > 0 due to
  // high-watermark.
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Read)).Times(0);
  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*read_filter, onData(_, false)).WillOnce(Return(FilterStatus::Continue));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());

  // Already read disabled, expect no changes to enabled events mask.
  EXPECT_CALL(*file_event_, setEnabled(_)).Times(0);
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(true));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(false));
  // Read buffer is at the high watermark so read_disable_count should be == 1. Expect a read
  // activate but no call to setEnable to change the registration mask.
  EXPECT_CALL(*file_event_, setEnabled(_)).Times(0);
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Read));
  EXPECT_EQ(Connection::ReadDisableStatus::StillReadDisabled, connection_->readDisable(false));

  // Invoke the file event cb while read_disable_count_ == 1 and fully drain the read buffer.
  // Expect no transport reads. Expect a read resumption due to transport_wants_read_ being true
  // when read is re-enabled due to going under the low watermark.
  EXPECT_CALL(*transport_socket_, doRead(_)).Times(0);
  EXPECT_CALL(*read_filter, onData(_, _))
      .WillRepeatedly(Invoke([&](Buffer::Instance& data, bool) -> FilterStatus {
        EXPECT_EQ(10, data.length());
        data.drain(data.length());
        return FilterStatus::Continue;
      }));
  // The buffer is fully drained. Expect a read activation because setTransportSocketIsReadable set
  // transport_wants_read_ and no transport doRead calls have happened.
  EXPECT_CALL(*file_event_, setEnabled(Event::FileReadyType::Read | Event::FileReadyType::Write));
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Read));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());

  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());

  // Verify there are no read activate calls the event callback does a transport read and clears the
  // transport_wants_read_ state.
  EXPECT_CALL(*file_event_, setEnabled(_));
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadDisabled,
            connection_->readDisable(true));
  EXPECT_CALL(*file_event_, setEnabled(_));
  EXPECT_CALL(*file_event_, activate(_)).Times(0);
  EXPECT_EQ(Connection::ReadDisableStatus::TransitionedToReadEnabled,
            connection_->readDisable(false));
}

// Test the connection correctly handle the transport socket read (data + RST) case.
TEST_F(MockTransportConnectionImplTest, ServerLargeReadResetClose) {
  InSequence s;

  std::shared_ptr<MockReadFilter> read_filter(new StrictMock<MockReadFilter>());
  connection_->addReadFilter(read_filter);

  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Invoke([](Buffer::Instance& buffer) -> IoResult {
        buffer.add("01234");
        return {PostIoAction::KeepOpen, 5, false};
      }));

  EXPECT_CALL(*read_filter, onNewConnection()).WillOnce(Return(FilterStatus::Continue));
  EXPECT_CALL(*read_filter, onData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) -> FilterStatus {
        EXPECT_EQ(5, data.length());
        data.drain(data.length());
        return FilterStatus::Continue;
      }));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());

  // This simulates the socket do {...} while read when there is processed data
  // with the last rest flag.
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Invoke([](Buffer::Instance& buffer) -> IoResult {
        buffer.add("5678");
        return {PostIoAction::Close, 4, false, Api::IoError::IoErrorCode::ConnectionReset};
      }));

  EXPECT_CALL(*read_filter, onData(_, _))
      .WillOnce(Invoke([&](Buffer::Instance& data, bool) -> FilterStatus {
        EXPECT_EQ(4, data.length());
        data.drain(data.length());
        return FilterStatus::Continue;
      }));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
}

// Test that BytesSentCb is invoked at the correct times
TEST_F(MockTransportConnectionImplTest, BytesSentCallback) {
  uint64_t bytes_sent = 0;
  uint64_t cb_called = 0;
  connection_->addBytesSentCallback([&](uint64_t arg) {
    cb_called++;
    bytes_sent = arg;
    return true;
  });

  // 100 bytes were sent; expect BytesSent event
  EXPECT_CALL(*transport_socket_, doWrite(_, _))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 100, false}));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Write).ok());
  EXPECT_EQ(cb_called, 1);
  EXPECT_EQ(bytes_sent, 100);
  cb_called = false;
  bytes_sent = 0;

  // 0 bytes were sent; no event
  EXPECT_CALL(*transport_socket_, doWrite(_, _))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Write).ok());
  EXPECT_EQ(cb_called, 0);

  // Reading should not cause BytesSent
  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 1, false}));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_EQ(cb_called, 0);

  // Closed event should not raise a BytesSent event (but does raise RemoteClose)
  EXPECT_CALL(callbacks_, onEvent(ConnectionEvent::RemoteClose));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Closed).ok());
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
    return true;
  });

  connection_->addBytesSentCallback([&](uint64_t arg) {
    cb_called2++;
    bytes_sent2 = arg;
    return true;
  });

  EXPECT_CALL(*transport_socket_, doWrite(_, _))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 100, false}));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Write).ok());
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
    return true;
  };
  connection_->addBytesSentCallback(cb);
  connection_->addBytesSentCallback(cb);

  EXPECT_CALL(*transport_socket_, doWrite(_, _))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 100, false}));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Write).ok());

  EXPECT_EQ(cb_called, 1);
  EXPECT_EQ(connection_->state(), Connection::State::Closed);
}

// Test that a callback may unsubscribe from being called in the future.
// Test defines 3 callbacks. First, the middle one unsubscribes and later
// on the last added callback unsubscribes.
TEST_F(MockTransportConnectionImplTest, BytesSentUnsubscribe) {
  uint32_t cb_called1 = 0;
  uint32_t cb_called2 = 0;
  uint32_t cb_called3 = 0;
  uint32_t bytes_sent2 = 0;
  uint32_t bytes_sent3 = 0;

  // The first callback should never unsubscribe.
  connection_->addBytesSentCallback([&](uint64_t) {
    cb_called1++;
    return true;
  });

  // The second callback should unsubscribe after sending 10 bytes.
  connection_->addBytesSentCallback([&](uint64_t arg) {
    cb_called2++;
    bytes_sent2 += arg;
    return (bytes_sent2 < 10);
  });

  // The third callback should unsubscribe after sending 20 bytes.
  connection_->addBytesSentCallback([&](uint64_t arg) {
    cb_called3++;
    bytes_sent3 += arg;
    return (bytes_sent3 < 20);
  });

  EXPECT_CALL(*transport_socket_, doWrite(_, _))
      .WillRepeatedly(Return(IoResult{PostIoAction::KeepOpen, 5, false}));
  // Send 5 bytes. All callbacks should be called.
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Write).ok());
  EXPECT_EQ(cb_called1, 1);
  EXPECT_EQ(cb_called2, 1);
  EXPECT_EQ(cb_called3, 1);

  // Send next 5 bytes. All callbacks should be called and the second
  // callback should unsubscribe.
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Write).ok());
  EXPECT_EQ(cb_called1, 2);
  EXPECT_EQ(cb_called2, 2);
  EXPECT_EQ(cb_called3, 2);

  // Send next 5 bytes. Only the first and third callbacks should be called.
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Write).ok());
  EXPECT_EQ(cb_called1, 3);
  EXPECT_EQ(cb_called2, 2);
  EXPECT_EQ(cb_called3, 3);

  // Send next 5 bytes. The first and third callbacks should be called and
  // the third one should unsubscribe.
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Write).ok());
  EXPECT_EQ(cb_called1, 4);
  EXPECT_EQ(cb_called2, 2);
  EXPECT_EQ(cb_called3, 4);

  // Send next 5 bytes. Only the first callback should be called.
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Write).ok());
  EXPECT_EQ(cb_called1, 5);
  EXPECT_EQ(cb_called2, 2);
  EXPECT_EQ(cb_called3, 4);
}

// Test that onWrite does not have end_stream set, with half-close disabled
TEST_F(MockTransportConnectionImplTest, FullCloseWrite) {
  const std::string val("some data");
  Buffer::OwnedImpl buffer(val);
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke(file_ready_cb_));
  EXPECT_CALL(*transport_socket_, doWrite(BufferStringEqual(val), false))
      .WillOnce(Invoke(simulateSuccessfulWrite));
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
      .WillOnce(Invoke(simulateSuccessfulWrite));
  connection_->write(buffer, false);

  EXPECT_CALL(*transport_socket_, doWrite(_, true)).WillOnce(Invoke(simulateSuccessfulWrite));
  connection_->write(buffer, true);
}

TEST_F(MockTransportConnectionImplTest, ReadMultipleEndStream) {
  std::shared_ptr<MockReadFilter> read_filter(new NiceMock<MockReadFilter>());
  connection_->enableHalfClose(true);
  connection_->addReadFilter(read_filter);
  EXPECT_CALL(*transport_socket_, doRead(_))
      .Times(2)
      .WillRepeatedly(Return(IoResult{PostIoAction::KeepOpen, 0, true}));
  EXPECT_CALL(*read_filter, onData(_, true));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
}

// Test that if both sides half-close, the connection is closed, with the read half-close coming
// first.
TEST_F(MockTransportConnectionImplTest, BothHalfCloseReadFirst) {
  std::shared_ptr<MockReadFilter> read_filter(new NiceMock<MockReadFilter>());
  connection_->enableHalfClose(true);
  connection_->addReadFilter(read_filter);

  EXPECT_CALL(*transport_socket_, doRead(_))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, true}));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());

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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());

  EXPECT_CALL(callbacks_, onEvent(ConnectionEvent::LocalClose));
  EXPECT_CALL(*transport_socket_, doWrite(_, true)).WillOnce(Invoke(simulateSuccessfulWrite));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Write).ok());
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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());

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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Write).ok());
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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());

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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
  EXPECT_CALL(*transport_socket_, doWrite(_, true))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, true}));
}

// Test the interface used by external consumers.
TEST_F(MockTransportConnectionImplTest, FlushWriteBufferAndRtt) {
  InSequence s;

  // Queue up some data in write buffer.
  const std::string val("some data");
  Buffer::OwnedImpl buffer(val);
  EXPECT_CALL(*file_event_, activate(Event::FileReadyType::Write)).WillOnce(Invoke(file_ready_cb_));
  EXPECT_CALL(*transport_socket_, doWrite(BufferStringEqual(val), false))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  connection_->write(buffer, false);

  // Make sure calling the rtt function doesn't cause problems.
  connection_->lastRoundTripTime();

  // A read event triggers underlying socket to ask for more data.
  EXPECT_CALL(*transport_socket_, doRead(_)).WillOnce(InvokeWithoutArgs([this] {
    transport_socket_callbacks_->flushWriteBuffer();
    return IoResult{PostIoAction::KeepOpen, 0, false};
  }));
  EXPECT_CALL(*transport_socket_, doWrite(BufferStringEqual(val), false))
      .WillOnce(Return(IoResult{PostIoAction::KeepOpen, 0, false}));
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
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
    EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());

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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
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
  EXPECT_TRUE(file_ready_cb_(Event::FileReadyType::Read).ok());
}

// Test that close(ConnectionCloseType::Abort) won't write and flush pending data.
TEST_F(PostCloseConnectionImplTest, CloseAbort) {
  InSequence s;
  initialize();
  writeSomeData();

  // Connection abort. We have data written above in writeSomeData(),
  // it won't be written and flushed due to ``ConnectionCloseType::Abort``.
  EXPECT_CALL(*transport_socket_, doWrite(_, true)).Times(0);
  EXPECT_CALL(*transport_socket_, closeSocket(_));
  connection_->close(ConnectionCloseType::Abort);
}

// Test that close(ConnectionCloseType::AbortReset) won't write and flush pending data.
TEST_F(PostCloseConnectionImplTest, AbortReset) {
  InSequence s;
  initialize();
  writeSomeData();

  // Connection abort. We have data written above in writeSomeData(),
  // it won't be written and flushed due to ``ConnectionCloseType::AbortReset``.
  EXPECT_CALL(*transport_socket_, doWrite(_, true)).Times(0);
  EXPECT_CALL(*transport_socket_, closeSocket(_));
  connection_->close(ConnectionCloseType::AbortReset);
}

class ReadBufferLimitTest : public ConnectionImplTest {
public:
  void readBufferLimitTest(uint32_t read_buffer_limit, uint32_t expected_chunk_size) {
    const uint32_t buffer_size = 256 * 1024;
    dispatcher_ = api_->allocateDispatcher("test_thread");
    socket_ = std::make_shared<Network::Test::TcpListenSocketImmediateListen>(
        Network::Test::getCanonicalLoopbackAddress(GetParam()));
    NiceMock<Network::MockListenerConfig> listener_config;
    Server::ThreadLocalOverloadStateOptRef overload_state;
    listener_ = std::make_unique<Network::TcpListenerImpl>(
        *dispatcher_, api_->randomGenerator(), runtime_, socket_, listener_callbacks_,
        listener_config.bindToPort(), listener_config.ignoreGlobalConnLimit(),
        listener_config.shouldBypassOverloadManager(),
        listener_config.maxConnectionsToAcceptPerSocketEvent(), overload_state);

    client_connection_ = dispatcher_->createClientConnection(
        socket_->connectionInfoProvider().localAddress(),
        Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr,
        nullptr);
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
    EXPECT_CALL(listener_callbacks_, recordConnectionsAcceptedOnSocketEvent(_));

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
        .WillOnce(InvokeWithoutArgs([&]() -> void { dispatcher_->exit(); }));
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_CALL(client_callbacks_, onEvent(ConnectionEvent::RemoteClose))
        .WillOnce(InvokeWithoutArgs([&]() -> void {
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
    address = *Utility::resolveUrl("tcp://255.255.255.255:1");
  } else {
    // IPv6 reserved multicast address.
    address = *Utility::resolveUrl("tcp://[ff00::]:1");
  }
  ClientConnectionPtr connection =
      dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                          Network::Test::createRawBufferSocket(), nullptr, nullptr);
  connection->connect();
  connection->noDelay(true);
  connection->close(ConnectionCloseType::NoFlush);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(TcpClientConnectionImplTest, BadConnectConnRefused) {
  // Connecting to an invalid port on localhost will cause ECONNREFUSED which is a different code
  // path from other errors. Test this also.
  ClientConnectionPtr connection = dispatcher_->createClientConnection(
      *Utility::resolveUrl(
          fmt::format("tcp://{}:1", Network::Test::getLoopbackAddressUrlString(GetParam()))),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr,
      nullptr);
  connection->connect();
  connection->noDelay(true);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_THAT(connection->transportFailureReason(), StartsWith("delayed connect error"));
}

TEST_P(TcpClientConnectionImplTest, BadConnectConnRefusedWithTransportError) {
  // Connecting to an invalid port on localhost will cause ECONNREFUSED which is a different code
  // path from other errors. Test this also.
  auto transport_socket = std::make_unique<NiceMock<MockTransportSocket>>();
  EXPECT_CALL(*transport_socket, failureReason()).WillRepeatedly(Return("custom error"));
  ClientConnectionPtr connection = dispatcher_->createClientConnection(
      *Utility::resolveUrl(
          fmt::format("tcp://{}:1", Network::Test::getLoopbackAddressUrlString(GetParam()))),
      Network::Address::InstanceConstSharedPtr(), std::move(transport_socket), nullptr, nullptr);
  connection->connect();
  connection->noDelay(true);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_THAT(connection->transportFailureReason(), StartsWith("delayed connect error"));
  EXPECT_THAT(connection->transportFailureReason(), EndsWith("custom error"));
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
      *Utility::resolveUrl("unix://" + path_), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), options, nullptr);
  connection->close(ConnectionCloseType::NoFlush);
}

// Validate we skip setting source address.
TEST_F(PipeClientConnectionImplTest, SkipSourceAddress) {
  ClientConnectionPtr connection = dispatcher_->createClientConnection(
      *Utility::resolveUrl("unix://" + path_), *Utility::resolveUrl("tcp://1.2.3.4:5"),
      Network::Test::createRawBufferSocket(), nullptr, nullptr);
  connection->close(ConnectionCloseType::NoFlush);
}

class InternalClientConnectionImplTest : public testing::Test {
protected:
  InternalClientConnectionImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  StrictMock<MockConnectionCallbacks> client_callbacks_;
};

// The internal address is passed to Envoy by EDS. If this Envoy instance is configured as internal
// address disabled, the EDS subscription should reject the config before dispatcher attempt to
// establish connection to such address.
TEST_F(InternalClientConnectionImplTest,
       CannotCreateConnectionToInternalAddressWithInternalAddressEnabled) {

  const Network::SocketInterface* sock_interface = Network::socketInterface(
      "envoy.extensions.network.socket_interface.default_socket_interface");
  Network::Address::InstanceConstSharedPtr address =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_0", "endpoint_id_0",
                                                                sock_interface);

  ASSERT_DEATH(
      {
        ClientConnectionPtr connection = dispatcher_->createClientConnection(
            address, Network::Address::InstanceConstSharedPtr(),
            Network::Test::createRawBufferSocket(), nullptr, nullptr);
      },
      "");
}

class ClientConnectionWithCustomRawBufferSocketTest : public ConnectionImplTestBase,
                                                      public testing::TestWithParam<Address::Type> {
protected:
  void setUpBasicConnection() {
    Address::InstanceConstSharedPtr address;
    switch (GetParam()) {
    case Address::Type::Pipe:
      address = *Utility::resolveUrl("unix://" + path_);
      break;
    case Address::Type::Ip:
    default:
      address = Network::Test::getCanonicalLoopbackAddress(Address::IpVersion::v4);
      break;
    }
    setUpBasicConnectionWithAddress(address);
  }

  class TestRawBufferSocket : public RawBufferSocket {
  public:
    bool compareCallbacks(TransportSocketCallbacks* callback) const {
      return this->transportSocketCallbacks() == callback;
    }
  };

  TransportSocketPtr createTransportSocket() override {
    return std::make_unique<TestRawBufferSocket>();
  }

  TestRawBufferSocket* getTransportSocket() const {
    Network::TestClientConnectionImpl* client_conn_impl = testClientConnection();
    return dynamic_cast<TestRawBufferSocket*>(client_conn_impl->transportSocket().get());
  }

private:
  const std::string path_{TestEnvironment::unixDomainSocketPath("foo")};
};

INSTANTIATE_TEST_SUITE_P(IpVersions, ClientConnectionWithCustomRawBufferSocketTest,
                         testing::ValuesIn({Address::Type::Ip, Address::Type::Pipe}));

class TestObject : public StreamInfo::FilterState::Object {};

TEST_P(ClientConnectionWithCustomRawBufferSocketTest, TransportSocketCallbacks) {
  StreamInfo::FilterStateImpl filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  auto filter_state_object = std::make_shared<TestObject>();
  filter_state.setData("test-filter-state", filter_state_object,
                       StreamInfo::FilterState::StateType::ReadOnly,
                       StreamInfo::FilterState::LifeSpan::Connection,
                       StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);
  transport_socket_options_ = TransportSocketOptionsUtility::fromFilterState(filter_state);
  setUpBasicConnection();

  EXPECT_TRUE(getTransportSocket()->compareCallbacks(testClientConnection()));
  EXPECT_TRUE(client_connection_->streamInfo().filterState()->hasDataWithName("test-filter-state"));

  disconnect(false);
}

} // namespace
} // namespace Network
} // namespace Envoy
