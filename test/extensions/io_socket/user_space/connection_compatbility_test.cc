#include <memory>

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
    local_addr_ = io_handle_->localAddress();
    remote_addr_ = io_handle_->peerAddress();
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
} // namespace
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
