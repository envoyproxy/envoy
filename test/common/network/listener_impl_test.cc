#include <limits>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/network/exception.h"

#include "common/network/address_impl.h"
#include "common/network/tcp_listener_impl.h"
#include "common/network/utility.h"
#include "common/stream_info/stream_info_impl.h"

#include "test/common/network/listener_impl_test_base.h"
#include "test/mocks/network/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

static void errorCallbackTest(Address::IpVersion version) {
  // Force the error callback to fire by closing the socket under the listener. We run this entire
  // test in the forked process to avoid confusion when the fork happens.
  Api::ApiPtr api = Api::createApiForTest();
  Event::DispatcherPtr dispatcher(api->allocateDispatcher("test_thread"));

  auto socket = std::make_shared<Network::TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(version), nullptr, true);
  Network::MockTcpListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher->createListener(socket, listener_callbacks, true, ENVOY_TCP_BACKLOG_SIZE);

  Network::ClientConnectionPtr client_connection = dispatcher->createClientConnection(
      socket->localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr);
  client_connection->connect();

  StreamInfo::StreamInfoImpl stream_info(dispatcher->timeSource());
  EXPECT_CALL(listener_callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& accepted_socket) -> void {
        Network::ConnectionPtr conn = dispatcher->createServerConnection(
            std::move(accepted_socket), Network::Test::createRawBufferSocket(), stream_info);
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        socket->close();
      }));

  dispatcher->run(Event::Dispatcher::RunType::Block);
}

class ListenerImplDeathTest : public testing::TestWithParam<Address::IpVersion> {};
INSTANTIATE_TEST_SUITE_P(IpVersions, ListenerImplDeathTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);
TEST_P(ListenerImplDeathTest, ErrorCallback) {
  EXPECT_DEATH(errorCallbackTest(GetParam()), ".*listener accept failure.*");
}

class TestTcpListenerImpl : public TcpListenerImpl {
public:
  TestTcpListenerImpl(Event::DispatcherImpl& dispatcher, Random::RandomGenerator& random_generator,
                      SocketSharedPtr socket, TcpListenerCallbacks& cb, bool bind_to_port,
                      uint32_t tcp_backlog = ENVOY_TCP_BACKLOG_SIZE)
      : TcpListenerImpl(dispatcher, random_generator, std::move(socket), cb, bind_to_port,
                        tcp_backlog) {}

  MOCK_METHOD(Address::InstanceConstSharedPtr, getLocalAddress, (os_fd_t fd));
};

using TcpListenerImplTest = ListenerImplTestBase;
INSTANTIATE_TEST_SUITE_P(IpVersions, TcpListenerImplTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

// Test that socket options are set after the listener is setup.
TEST_P(TcpListenerImplTest, SetListeningSocketOptionsSuccess) {
  Network::MockTcpListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Random::MockRandomGenerator random_generator;

  auto socket = std::make_shared<TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(version_), nullptr, true);
  std::shared_ptr<MockSocketOption> option = std::make_shared<MockSocketOption>();
  socket->addOption(option);
  EXPECT_CALL(*option, setOption(_, envoy::config::core::v3::SocketOption::STATE_LISTENING))
      .WillOnce(Return(true));
  TestTcpListenerImpl listener(dispatcherImpl(), random_generator, socket, listener_callbacks,
                               true);
}

// Test that an exception is thrown if there is an error setting socket options.
TEST_P(TcpListenerImplTest, SetListeningSocketOptionsError) {
  Network::MockTcpListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Random::MockRandomGenerator random_generator;

  auto socket = std::make_shared<TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(version_), nullptr, true);
  std::shared_ptr<MockSocketOption> option = std::make_shared<MockSocketOption>();
  socket->addOption(option);
  EXPECT_CALL(*option, setOption(_, envoy::config::core::v3::SocketOption::STATE_LISTENING))
      .WillOnce(Return(false));
  EXPECT_THROW_WITH_MESSAGE(
      TestTcpListenerImpl(dispatcherImpl(), random_generator, socket, listener_callbacks, true),
      CreateListenerException,
      fmt::format("cannot set post-listen socket option on socket: {}",
                  socket->localAddress()->asString()));
}

TEST_P(TcpListenerImplTest, UseActualDst) {
  auto socket = std::make_shared<TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(version_), nullptr, true);
  auto socketDst = std::make_shared<TcpListenSocket>(alt_address_, nullptr, false);
  Network::MockTcpListenerCallbacks listener_callbacks1;
  Network::MockConnectionHandler connection_handler;
  Random::MockRandomGenerator random_generator;
  // Do not redirect since use_original_dst is false.
  Network::TestTcpListenerImpl listener(dispatcherImpl(), random_generator, socket,
                                        listener_callbacks1, true);
  Network::MockTcpListenerCallbacks listener_callbacks2;
  Network::TestTcpListenerImpl listenerDst(dispatcherImpl(), random_generator, socketDst,
                                           listener_callbacks2, false);

  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      socket->localAddress(), Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr);
  client_connection->connect();

  EXPECT_CALL(listener, getLocalAddress(_)).Times(0);

  StreamInfo::StreamInfoImpl stream_info(dispatcher_->timeSource());
  EXPECT_CALL(listener_callbacks2, onAccept_(_)).Times(0);
  EXPECT_CALL(listener_callbacks1, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& accepted_socket) -> void {
        Network::ConnectionPtr conn = dispatcher_->createServerConnection(
            std::move(accepted_socket), Network::Test::createRawBufferSocket(), stream_info);
        EXPECT_EQ(*conn->localAddress(), *socket->localAddress());
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(TcpListenerImplTest, GlobalConnectionLimitEnforcement) {
  // Required to manipulate runtime values when there is no test server.
  TestScopedRuntime scoped_runtime;

  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"overload.global_downstream_max_connections", "2"}});
  auto socket = std::make_shared<Network::TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(version_), nullptr, true);
  Network::MockTcpListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Network::ListenerPtr listener =
      dispatcher_->createListener(socket, listener_callbacks, true, ENVOY_TCP_BACKLOG_SIZE);

  std::vector<Network::ClientConnectionPtr> client_connections;
  std::vector<Network::ConnectionPtr> server_connections;
  StreamInfo::StreamInfoImpl stream_info(dispatcher_->timeSource());
  EXPECT_CALL(listener_callbacks, onAccept_(_))
      .WillRepeatedly(Invoke([&](Network::ConnectionSocketPtr& accepted_socket) -> void {
        server_connections.emplace_back(dispatcher_->createServerConnection(
            std::move(accepted_socket), Network::Test::createRawBufferSocket(), stream_info));
        dispatcher_->exit();
      }));

  auto initiate_connections = [&](const int count) {
    for (int i = 0; i < count; ++i) {
      client_connections.emplace_back(dispatcher_->createClientConnection(
          socket->localAddress(), Network::Address::InstanceConstSharedPtr(),
          Network::Test::createRawBufferSocket(), nullptr));
      client_connections.back()->connect();
    }
  };

  initiate_connections(5);
  EXPECT_CALL(listener_callbacks, onReject(TcpListenerCallbacks::RejectCause::GlobalCxLimit))
      .Times(3);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // We expect any server-side connections that get created to populate 'server_connections'.
  EXPECT_EQ(2, server_connections.size());

  // Let's increase the allowed connections and try sending more connections.
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"overload.global_downstream_max_connections", "3"}});
  initiate_connections(5);
  EXPECT_CALL(listener_callbacks, onReject(TcpListenerCallbacks::RejectCause::GlobalCxLimit))
      .Times(4);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(3, server_connections.size());

  // Clear the limit and verify there's no longer a limit.
  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"overload.global_downstream_max_connections", ""}});
  initiate_connections(10);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(13, server_connections.size());

  for (const auto& conn : client_connections) {
    conn->close(ConnectionCloseType::NoFlush);
  }
  for (const auto& conn : server_connections) {
    conn->close(ConnectionCloseType::NoFlush);
  }

  Runtime::LoaderSingleton::getExisting()->mergeValues(
      {{"overload.global_downstream_max_connections", ""}});
}

TEST_P(TcpListenerImplTest, WildcardListenerUseActualDst) {
  auto socket = std::make_shared<TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(version_), nullptr, true);
  Network::MockTcpListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Random::MockRandomGenerator random_generator;
  // Do not redirect since use_original_dst is false.
  Network::TestTcpListenerImpl listener(dispatcherImpl(), random_generator, socket,
                                        listener_callbacks, true);

  auto local_dst_address = Network::Utility::getAddressWithPort(
      *Network::Test::getCanonicalLoopbackAddress(version_), socket->localAddress()->ip()->port());
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      local_dst_address, Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr);
  client_connection->connect();

  StreamInfo::StreamInfoImpl stream_info(dispatcher_->timeSource());
  EXPECT_CALL(listener_callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        Network::ConnectionPtr conn = dispatcher_->createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket(), stream_info);
        EXPECT_EQ(*conn->localAddress(), *local_dst_address);
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Test for the correct behavior when a listener is configured with an ANY address that allows
// receiving IPv4 connections on an IPv6 socket. In this case the address instances of both
// local and remote addresses of the connection should be IPv4 instances, as the connection really
// is an IPv4 connection.
TEST_P(TcpListenerImplTest, WildcardListenerIpv4Compat) {
  auto option = std::make_unique<MockSocketOption>();
  auto options = std::make_shared<std::vector<Network::Socket::OptionConstSharedPtr>>();
  EXPECT_CALL(*option, setOption(_, envoy::config::core::v3::SocketOption::STATE_PREBIND))
      .WillOnce(Return(true));
  options->emplace_back(std::move(option));

  auto socket = std::make_shared<TcpListenSocket>(Network::Test::getAnyAddress(version_, true),
                                                  options, true);
  Network::MockTcpListenerCallbacks listener_callbacks;
  Network::MockConnectionHandler connection_handler;
  Random::MockRandomGenerator random_generator;

  ASSERT_TRUE(socket->localAddress()->ip()->isAnyAddress());

  // Do not redirect since use_original_dst is false.
  Network::TestTcpListenerImpl listener(dispatcherImpl(), random_generator, socket,
                                        listener_callbacks, true);

  auto listener_address = Network::Utility::getAddressWithPort(
      *Network::Test::getCanonicalLoopbackAddress(version_), socket->localAddress()->ip()->port());
  auto local_dst_address = Network::Utility::getAddressWithPort(
      *Network::Utility::getCanonicalIpv4LoopbackAddress(), socket->localAddress()->ip()->port());
  Network::ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
      local_dst_address, Network::Address::InstanceConstSharedPtr(),
      Network::Test::createRawBufferSocket(), nullptr);
  client_connection->connect();

  StreamInfo::StreamInfoImpl stream_info(dispatcher_->timeSource());
  EXPECT_CALL(listener_callbacks, onAccept_(_))
      .WillOnce(Invoke([&](Network::ConnectionSocketPtr& socket) -> void {
        Network::ConnectionPtr conn = dispatcher_->createServerConnection(
            std::move(socket), Network::Test::createRawBufferSocket(), stream_info);
        EXPECT_EQ(conn->localAddress()->ip()->version(), conn->remoteAddress()->ip()->version());
        EXPECT_EQ(conn->localAddress()->asString(), local_dst_address->asString());
        EXPECT_EQ(*conn->localAddress(), *local_dst_address);
        client_connection->close(ConnectionCloseType::NoFlush);
        conn->close(ConnectionCloseType::NoFlush);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(TcpListenerImplTest, DisableAndEnableListener) {
  testing::InSequence s1;

  auto socket = std::make_shared<TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(version_), nullptr, true);
  MockTcpListenerCallbacks listener_callbacks;
  MockConnectionCallbacks connection_callbacks;
  Random::MockRandomGenerator random_generator;
  TestTcpListenerImpl listener(dispatcherImpl(), random_generator, socket, listener_callbacks,
                               true);

  // When listener is disabled, the timer should fire before any connection is accepted.
  listener.disable();

  ClientConnectionPtr client_connection =
      dispatcher_->createClientConnection(socket->localAddress(), Address::InstanceConstSharedPtr(),
                                          Network::Test::createRawBufferSocket(), nullptr);
  client_connection->addConnectionCallbacks(connection_callbacks);
  client_connection->connect();

  EXPECT_CALL(listener_callbacks, onAccept_(_)).Times(0);
  EXPECT_CALL(connection_callbacks, onEvent(_))
      .WillOnce(Invoke([&](Network::ConnectionEvent event) -> void {
        EXPECT_EQ(event, Network::ConnectionEvent::Connected);
        dispatcher_->exit();
      }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // When the listener is re-enabled, the pending connection should be accepted.
  listener.enable();

  EXPECT_CALL(listener_callbacks, onAccept_(_)).WillOnce(Invoke([&](ConnectionSocketPtr&) -> void {
    client_connection->close(ConnectionCloseType::NoFlush);
  }));
  EXPECT_CALL(connection_callbacks, onEvent(_))
      .WillOnce(Invoke([&](Network::ConnectionEvent event) -> void {
        EXPECT_NE(event, Network::ConnectionEvent::Connected);
        dispatcher_->exit();
      }));

  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

TEST_P(TcpListenerImplTest, SetListenerRejectFractionZero) {
  auto socket = std::make_shared<TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(version_), nullptr, true);
  MockTcpListenerCallbacks listener_callbacks;
  MockConnectionCallbacks connection_callbacks;
  Random::MockRandomGenerator random_generator;
  TestTcpListenerImpl listener(dispatcherImpl(), random_generator, socket, listener_callbacks,
                               true);

  listener.setRejectFraction(0);

  // This connection will be accepted and not rejected.
  {
    testing::InSequence s1;
    EXPECT_CALL(connection_callbacks, onEvent(ConnectionEvent::Connected));
    EXPECT_CALL(connection_callbacks, onEvent(ConnectionEvent::LocalClose));
  }
  EXPECT_CALL(listener_callbacks, onAccept_(_)).WillOnce([&] { dispatcher_->exit(); });

  ClientConnectionPtr client_connection =
      dispatcher_->createClientConnection(socket->localAddress(), Address::InstanceConstSharedPtr(),
                                          Network::Test::createRawBufferSocket(), nullptr);
  client_connection->addConnectionCallbacks(connection_callbacks);
  client_connection->connect();
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Now that we've seen that the connection hasn't been closed by the listener, make sure to close
  // it.
  client_connection->close(ConnectionCloseType::NoFlush);
}

TEST_P(TcpListenerImplTest, SetListenerRejectFractionIntermediate) {
  auto socket = std::make_shared<TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(version_), nullptr, true);
  MockTcpListenerCallbacks listener_callbacks;
  MockConnectionCallbacks connection_callbacks;
  Random::MockRandomGenerator random_generator;
  TestTcpListenerImpl listener(dispatcherImpl(), random_generator, socket, listener_callbacks,
                               true);

  listener.setRejectFraction(0.5f);

  // The first connection will be rejected because the random value is too small.
  {
    testing::InSequence s1;
    EXPECT_CALL(random_generator, random()).WillOnce(Return(0));
    EXPECT_CALL(listener_callbacks, onReject(TcpListenerCallbacks::RejectCause::OverloadAction));
  }
  {
    testing::InSequence s2;
    EXPECT_CALL(connection_callbacks, onEvent(ConnectionEvent::Connected));
    EXPECT_CALL(connection_callbacks, onEvent(ConnectionEvent::RemoteClose)).WillOnce([&] {
      dispatcher_->exit();
    });
  }

  {
    ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
        socket->localAddress(), Address::InstanceConstSharedPtr(),
        Network::Test::createRawBufferSocket(), nullptr);
    client_connection->addConnectionCallbacks(connection_callbacks);
    client_connection->connect();
    dispatcher_->run(Event::Dispatcher::RunType::Block);
  }

  // The second connection rolls better on initiative and is accepted.
  {
    testing::InSequence s1;
    EXPECT_CALL(random_generator, random()).WillOnce(Return(std::numeric_limits<uint64_t>::max()));
    EXPECT_CALL(listener_callbacks, onAccept_(_));
  }
  {
    testing::InSequence s2;
    EXPECT_CALL(connection_callbacks, onEvent(ConnectionEvent::Connected)).WillOnce([&] {
      dispatcher_->exit();
    });
    EXPECT_CALL(connection_callbacks, onEvent(ConnectionEvent::RemoteClose)).Times(0);
  }

  {
    ClientConnectionPtr client_connection = dispatcher_->createClientConnection(
        socket->localAddress(), Address::InstanceConstSharedPtr(),
        Network::Test::createRawBufferSocket(), nullptr);
    client_connection->addConnectionCallbacks(connection_callbacks);
    client_connection->connect();
    dispatcher_->run(Event::Dispatcher::RunType::Block);

    EXPECT_CALL(connection_callbacks, onEvent(ConnectionEvent::LocalClose));
    // Now that we've seen that the connection hasn't been closed by the listener, make sure to
    // close it.
    client_connection->close(ConnectionCloseType::NoFlush);
  }
}

TEST_P(TcpListenerImplTest, SetListenerRejectFractionAll) {
  auto socket = std::make_shared<TcpListenSocket>(
      Network::Test::getCanonicalLoopbackAddress(version_), nullptr, true);
  MockTcpListenerCallbacks listener_callbacks;
  MockConnectionCallbacks connection_callbacks;
  Random::MockRandomGenerator random_generator;
  TestTcpListenerImpl listener(dispatcherImpl(), random_generator, socket, listener_callbacks,
                               true);

  listener.setRejectFraction(1);

  {
    testing::InSequence s1;
    EXPECT_CALL(listener_callbacks, onReject(TcpListenerCallbacks::RejectCause::OverloadAction));
  }

  {
    testing::InSequence s2;
    EXPECT_CALL(connection_callbacks, onEvent(ConnectionEvent::Connected));
    EXPECT_CALL(connection_callbacks, onEvent(ConnectionEvent::RemoteClose)).WillOnce([&] {
      dispatcher_->exit();
    });
  }

  ClientConnectionPtr client_connection =
      dispatcher_->createClientConnection(socket->localAddress(), Address::InstanceConstSharedPtr(),
                                          Network::Test::createRawBufferSocket(), nullptr);
  client_connection->addConnectionCallbacks(connection_callbacks);
  client_connection->connect();
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

} // namespace
} // namespace Network
} // namespace Envoy
