#include <memory>

#include "source/common/network/address_impl.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listen_socket_impl.h"
#include "source/common/network/raw_buffer_socket.h"
#include "source/common/network/utility.h"
#include "source/extensions/io_socket/user_space/io_handle_impl.h"

#include "test/mocks/network/mocks.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {
namespace {

// This class verifies client connection can be established with user space socket.
class InternalClientConnectionImplTest : public testing::Test {
public:
  InternalClientConnectionImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void SetUp() override {
    std::tie(io_handle_, io_handle_peer_) = IoHandleFactory::createIoHandlePair();
    local_addr_ = *io_handle_->localAddress();
    remote_addr_ = *io_handle_->peerAddress();
  }
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::unique_ptr<IoHandleImpl> io_handle_;
  std::unique_ptr<IoHandleImpl> io_handle_peer_;
  Network::MockConnectionCallbacks connection_callbacks;
  std::unique_ptr<Network::ClientConnectionImpl> client_;
  Network::Address::InstanceConstSharedPtr local_addr_;
  Network::Address::InstanceConstSharedPtr remote_addr_;
};

TEST_F(InternalClientConnectionImplTest, Basic) {
  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->connect();
  client_->noDelay(true);
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  client_->close(Network::ConnectionCloseType::NoFlush);
}

TEST_F(InternalClientConnectionImplTest, ConnectCallbacksAreInvoked) {
  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->addConnectionCallbacks(connection_callbacks);
  client_->connect();
  client_->noDelay(true);
  EXPECT_CALL(connection_callbacks, onEvent(_))
      .WillOnce(Invoke([&](Network::ConnectionEvent event) -> void {
        EXPECT_EQ(event, Network::ConnectionEvent::Connected);
        dispatcher_->exit();
      }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));

  client_->close(Network::ConnectionCloseType::NoFlush);
}

TEST_F(InternalClientConnectionImplTest, ConnectFailed) {
  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->addConnectionCallbacks(connection_callbacks);
  client_->connect();
  client_->noDelay(true);

  io_handle_peer_->close();
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  client_->close(Network::ConnectionCloseType::NoFlush);
}

// With half-close enabled and the feature on, a peer full close fires RemoteClose. Without
// this, tcp_proxy CONNECT tunneling through an internal_listener leaks upstream streams.
TEST_F(InternalClientConnectionImplTest, HalfCloseEnabledPeerFullClosePropagatesRemoteClose) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.internal_listener_peer_destroyed_propagation", "true"}});

  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->enableHalfClose(true);
  client_->addConnectionCallbacks(connection_callbacks);
  client_->connect();
  client_->noDelay(true);

  io_handle_peer_->close();
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  client_->close(Network::ConnectionCloseType::NoFlush);
}

// Feature off: legacy half-close behavior preserved. No RemoteClose on peer full close.
// Guards that the runtime flag actually gates the new behavior.
TEST_F(InternalClientConnectionImplTest, HalfCloseEnabledPeerFullCloseLegacyBehavior) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.internal_listener_peer_destroyed_propagation", "false"}});

  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->enableHalfClose(true);
  client_->addConnectionCallbacks(connection_callbacks);
  client_->connect();
  client_->noDelay(true);

  io_handle_peer_->close();
  // No RemoteClose expected with the feature off; loop will exit on Connected callback.
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  client_->close(Network::ConnectionCloseType::NoFlush);
}

// Feature on, peer shutdown(WR) only: still observed as half-close, NOT RemoteClose.
// Guards against regression on legitimate half-close patterns through internal_listener.
TEST_F(InternalClientConnectionImplTest, HalfCloseEnabledPeerShutdownWritePreservesHalfClose) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.internal_listener_peer_destroyed_propagation", "true"}});

  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->enableHalfClose(true);
  client_->addConnectionCallbacks(connection_callbacks);
  client_->connect();
  client_->noDelay(true);

  // Peer half-closes (shutdown(WR)) instead of fully closing. RemoteClose must not fire.
  io_handle_peer_->shutdown(ENVOY_SHUT_WR);
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce(Invoke([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); }));
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  client_->close(Network::ConnectionCloseType::NoFlush);
}
} // namespace
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
