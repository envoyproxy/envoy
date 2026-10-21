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

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"

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

class InternalFlushWriteConnectionImplTest : public InternalClientConnectionImplTest {
public:
  void TearDown() override {
    if (client_ && client_->state() != Network::Connection::State::Closed) {
      client_->removeConnectionCallbacks(connection_callbacks);
      client_->close(Network::ConnectionCloseType::NoFlush);
    }
  }
};

TEST_F(InternalFlushWriteConnectionImplTest, FlushWriteCompletesWhenBlockedPeerCloses) {
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
  // blocked write even though its EOF notification cannot deliver a Read event here.
  EXPECT_CALL(connection_callbacks, onEvent(Network::ConnectionEvent::RemoteClose));
  ASSERT_TRUE(io_handle_peer_->close().ok());
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  EXPECT_EQ(Network::Connection::State::Closed, client_->state());
  EXPECT_TRUE(*pending_write_drained);
}

} // namespace
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
