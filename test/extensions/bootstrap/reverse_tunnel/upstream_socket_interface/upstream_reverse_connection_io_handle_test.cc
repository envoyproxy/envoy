#include "envoy/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/v3/upstream_reverse_connection_socket_interface.pb.h"
#include "envoy/network/socket_interface.h"
#include "envoy/server/factory_context.h"
#include "envoy/thread_local/thread_local.h"

#include "source/common/network/address_impl.h"
#include "source/common/network/socket_interface.h"
#include "source/common/network/utility.h"
#include "source/common/thread_local/thread_local_impl.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_tunnel_acceptor.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/server/factory_context.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/test_runtime.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class TestUpstreamReverseConnectionIOHandle : public testing::Test {
protected:
  TestUpstreamReverseConnectionIOHandle() {
    mock_socket_ = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));
    EXPECT_CALL(*mock_socket_, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    mock_socket_->io_handle_ = std::move(mock_io_handle);

    io_handle_ = std::make_unique<UpstreamReverseConnectionIOHandle>(std::move(mock_socket_),
                                                                     "test-cluster");
  }

  void TearDown() override { io_handle_.reset(); }

  std::unique_ptr<NiceMock<Network::MockConnectionSocket>> mock_socket_;
  std::unique_ptr<UpstreamReverseConnectionIOHandle> io_handle_;
};

TEST_F(TestUpstreamReverseConnectionIOHandle, ConnectReturnsSuccess) {
  auto address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8080);

  auto result = io_handle_->connect(address);

  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.errno_, 0);
}

TEST_F(TestUpstreamReverseConnectionIOHandle, CloseCleansUpSocket) {
  auto result = io_handle_->close();

  EXPECT_EQ(result.err_, nullptr);
}

TEST_F(TestUpstreamReverseConnectionIOHandle, GetSocketReturnsConstReference) {
  const auto& socket = io_handle_->getSocket();

  EXPECT_NE(&socket, nullptr);
}

class UpstreamReverseConnectionIOHandleTest : public testing::Test {
protected:
  void SetUp() override {
    auto socket = std::make_unique<NiceMock<Network::MockConnectionSocket>>();

    auto mock_io_handle = std::make_unique<NiceMock<Network::MockIoHandle>>();
    EXPECT_CALL(*mock_io_handle, fdDoNotUse()).WillRepeatedly(Return(123));
    EXPECT_CALL(*socket, ioHandle()).WillRepeatedly(ReturnRef(*mock_io_handle));

    socket->io_handle_ = std::move(mock_io_handle);

    handle_ =
        std::make_unique<UpstreamReverseConnectionIOHandle>(std::move(socket), "test-cluster");
  }

  std::unique_ptr<UpstreamReverseConnectionIOHandle> handle_;
};

TEST_F(UpstreamReverseConnectionIOHandleTest, ConnectReturnsSuccess) {
  auto address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8080);

  auto result = handle_->connect(address);

  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.errno_, 0);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, GetSocketReturnsValidReference) {
  const auto& socket = handle_->getSocket();
  EXPECT_NE(&socket, nullptr);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
