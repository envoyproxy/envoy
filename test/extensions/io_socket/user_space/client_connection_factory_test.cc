#include "envoy/buffer/buffer.h"
#include "envoy/event/file_event.h"
#include "envoy/network/listen_socket.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/common/fancy_logger.h"
#include "source/common/network/address_impl.h"
#include "source/extensions/io_socket/user_space/io_handle_impl.h"
#include "test/test_common/network_utility.h"

#include "test/mocks/event/mocks.h"

#include "absl/container/fixed_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {
namespace {
class ClientConnectionFactoryTest : public testing::Test {
public:
  ClientConnectionFactoryTest() : buf_(1024) {
    std::tie(io_handle_, io_handle_peer_) = IoHandleFactory::createIoHandlePair();
  }

  NiceMock<Event::MockDispatcher> dispatcher_;

  // Owned by IoHandleImpl.
  NiceMock<Event::MockSchedulableCallback>* schedulable_cb_;
  std::unique_ptr<IoHandleImpl> io_handle_;
  std::unique_ptr<IoHandleImpl> io_handle_peer_;
  absl::FixedArray<char> buf_;
  Network::Address::EnvoyInternalInstance listener_addr{"listener_internal_address"};
};

class MockInternalListenerCallbacks : public Network::InternalListenerCallbacks {
public:
  MOCK_METHOD(void, onAccept, (Network::ConnectionSocketPtr &&));
  MOCK_METHOD(Event::Dispatcher&, dispatcher, ());
};

class MockInternalListenerManger : public Network::InternalListenerManagerOptRef {
public:
  MOCK_METHOD(Network::InternalListenerCallbacksOptRef, findByAddress,
              (const Network::Address::InstanceConstSharedPtr&));
};

TEST_F(ClientConnectionFactoryTest, Basic) {
  auto client_conn = dispatcher_.createClientConnection(
      std::make_shared<Network::Address::EnvoyInternalInstance>(listener_addr),
      Network::Address::InstanceConstSharedPtr(), Network::Test::createRawBufferSocket(), nullptr);
  client_conn->close(Network::ConnectionCloseType::NoFlush);
}
TEST_F(ClientConnectionFactoryTest, BasicRecv) {
  Buffer::OwnedImpl buf_to_write("0123456789");
  io_handle_peer_->write(buf_to_write);
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    ASSERT_EQ(10, result.return_value_);
    ASSERT_EQ("0123456789", absl::string_view(buf_.data(), result.return_value_));
  }
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    // `EAGAIN`.
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  }
  {
    io_handle_->setWriteEnd();
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    EXPECT_TRUE(result.ok());
  }
}

} // namespace
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy