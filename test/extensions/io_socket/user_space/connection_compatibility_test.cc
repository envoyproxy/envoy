#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
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

using testing::NiceMock;

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
      .WillOnce([&](Network::ConnectionEvent event) -> void {
        EXPECT_EQ(event, Network::ConnectionEvent::Connected);
        dispatcher_->exit();
      });
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

class InternalFlushWriteConnectionImplTest : public InternalClientConnectionImplTest,
                                             public testing::WithParamInterface<bool> {
public:
  void TearDown() override {
    if (client_ && client_->state() != Network::Connection::State::Closed) {
      client_->removeConnectionCallbacks(connection_callbacks);
      client_->close(Network::ConnectionCloseType::NoFlush);
    }
  }
};

TEST_P(InternalFlushWriteConnectionImplTest, FlushWriteCompletesWhenBlockedPeerCloses) {
  // The write wakeup must work with and without read-side reset emulation.
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues({{"envoy.reloadable_features.enable_send_rst_on_user_space_socket",
                               GetParam() ? "true" : "false"}});

  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->enableHalfClose(true);
  client_->addConnectionCallbacks(connection_callbacks);
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::Connected));
  client_->connect();
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  // Fill the peer directly, then queue a separate write in ConnectionImpl. This avoids relying
  // on OS buffering or on the number of event-loop iterations needed to produce backpressure.
  io_handle_peer_->setWatermarks(128);
  Buffer::OwnedImpl initial_data(std::string(256, 'a'));
  ASSERT_TRUE(client_->ioHandle().write(initial_data).ok());
  ASSERT_FALSE(io_handle_peer_->canReceiveData());

  auto pending_write_drained = std::make_shared<bool>(false);
  Buffer::OwnedImpl pending_data("pending response");
  pending_data.addDrainTracker([pending_write_drained]() { *pending_write_drained = true; });
  client_->write(pending_data, false);
  client_->close(Network::ConnectionCloseType::FlushWrite);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  ASSERT_EQ(Network::Connection::State::Closing, client_->state());
  EXPECT_FALSE(*pending_write_drained);

  // Half-close makes CloseAfterFlush listen only for Write. Closing the peer must wake that
  // blocked write even though its EOF/reset notification cannot deliver a Read event here.
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  ASSERT_TRUE(io_handle_peer_->close().ok());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, client_->state());
  EXPECT_TRUE(*pending_write_drained);
}

INSTANTIATE_TEST_SUITE_P(ResetEmulation, InternalFlushWriteConnectionImplTest, testing::Bool());

TEST_F(InternalClientConnectionImplTest, AbortResetEmitsConnectionResetToPeerGuardEnabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.enable_send_rst_on_user_space_socket", "true"}});

  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->addConnectionCallbacks(connection_callbacks);
  client_->connect();
  client_->noDelay(true);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  client_->close(Network::ConnectionCloseType::AbortReset);

  Buffer::OwnedImpl read_buf;
  auto read_res = io_handle_peer_->read(read_buf, 1024);
  EXPECT_FALSE(read_res.ok());
  ASSERT_NE(nullptr, read_res.err_);
  EXPECT_EQ(Network::IoSocketError::IoErrorCode::ConnectionReset, read_res.err_->getErrorCode());
}

TEST_F(InternalClientConnectionImplTest, AbortResetEmitsEofToPeerGuardDisabled) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.enable_send_rst_on_user_space_socket", "false"}});

  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->addConnectionCallbacks(connection_callbacks);
  client_->connect();
  client_->noDelay(true);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  client_->close(Network::ConnectionCloseType::AbortReset);

  Buffer::OwnedImpl read_buf;
  auto read_res = io_handle_peer_->read(read_buf, 1024);
  EXPECT_TRUE(read_res.ok());
  EXPECT_EQ(0, read_res.return_value_);
  EXPECT_EQ(nullptr, read_res.err_);
}

// With half-close enabled and the feature on, a peer full close surfaces as ECONNRESET once
// pending data drains and fires RemoteClose. Without this, tcp_proxy CONNECT tunneling through
// an internal_listener leaks upstream streams. The connection is fully connected before the
// peer closes so the close is observed on the read path, not through connect completion.
TEST_F(InternalClientConnectionImplTest, HalfCloseEnabledPeerFullClosePropagatesRemoteClose) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.enable_send_rst_on_user_space_socket", "true"}});

  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->enableHalfClose(true);
  auto read_filter = std::make_shared<NiceMock<Network::MockReadFilter>>();
  client_->addReadFilter(read_filter);
  client_->addConnectionCallbacks(connection_callbacks);
  client_->connect();
  client_->noDelay(true);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); });
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  io_handle_peer_->close();
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); });
  dispatcher_->run(Event::Dispatcher::RunType::Block);
}

// Feature off: legacy half-close behavior preserved. No RemoteClose on peer full close.
// Guards that the runtime flag actually gates the new behavior.
TEST_F(InternalClientConnectionImplTest, HalfCloseEnabledPeerFullCloseLegacyBehavior) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.enable_send_rst_on_user_space_socket", "false"}});

  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->enableHalfClose(true);
  auto read_filter = std::make_shared<NiceMock<Network::MockReadFilter>>();
  client_->addReadFilter(read_filter);
  client_->addConnectionCallbacks(connection_callbacks);
  client_->connect();
  client_->noDelay(true);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); });
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // With the feature off, the peer full close reads as a half-close: no RemoteClose.
  io_handle_peer_->close();
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose)).Times(0);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  client_->close(Network::ConnectionCloseType::NoFlush);
}

// Feature on, peer shutdown(WR) only: still observed as half-close, NOT RemoteClose.
// Guards against regression on legitimate half-close patterns through internal_listener.
TEST_F(InternalClientConnectionImplTest, HalfCloseEnabledPeerShutdownWritePreservesHalfClose) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.enable_send_rst_on_user_space_socket", "true"}});

  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->enableHalfClose(true);
  auto read_filter = std::make_shared<NiceMock<Network::MockReadFilter>>();
  client_->addReadFilter(read_filter);
  client_->addConnectionCallbacks(connection_callbacks);
  client_->connect();
  client_->noDelay(true);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); });
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Peer half-closes (shutdown(WR)) instead of fully closing. RemoteClose must not fire.
  io_handle_peer_->shutdown(ENVOY_SHUT_WR);
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose)).Times(0);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::LocalClose));
  client_->close(Network::ConnectionCloseType::NoFlush);
}

// Feature on: shutdown(WR) (write with end_stream=true), followed by peer calling close(). The
// peer's close completes a graceful four-way shutdown (bothSidesHalfClosed()). The connection
// terminates with RemoteClose and DetectedCloseType::Normal.
TEST_F(InternalClientConnectionImplTest,
       HalfCloseEnabledShutdownWriteThenPeerCloseEmitsCleanRemoteClose) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.enable_send_rst_on_user_space_socket", "true"}});

  client_ = std::make_unique<Network::ClientConnectionImpl>(
      *dispatcher_,
      std::make_unique<Network::ConnectionSocketImpl>(std::move(io_handle_), local_addr_,
                                                      remote_addr_),
      nullptr, std::make_unique<Network::RawBufferSocket>(), nullptr, nullptr);
  client_->enableHalfClose(true);
  auto read_filter = std::make_shared<NiceMock<Network::MockReadFilter>>();
  client_->addReadFilter(read_filter);
  client_->addConnectionCallbacks(connection_callbacks);
  client_->connect();
  client_->noDelay(true);

  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::Connected))
      .WillOnce([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); });
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  // Client half-closes its write side.
  Buffer::OwnedImpl empty_buf;
  client_->write(empty_buf, /*end_stream=*/true);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_TRUE(io_handle_peer_->hasReceivedEof());

  // Peer then fully closes. Because client already sent EOF, this completes the graceful
  // four-way shutdown. The connection must fire RemoteClose with DetectedCloseType::Normal.
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose))
      .WillOnce([&](Network::ConnectionEvent) -> void { dispatcher_->exit(); });
  io_handle_peer_->close();
  dispatcher_->run(Event::Dispatcher::RunType::Block);

  EXPECT_EQ(StreamInfo::DetectedCloseType::Normal, client_->detectedCloseType());
}
} // namespace
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
