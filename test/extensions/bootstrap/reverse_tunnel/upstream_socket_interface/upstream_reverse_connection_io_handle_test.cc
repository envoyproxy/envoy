#include <unistd.h>

#include "source/common/network/utility.h"
#include "source/extensions/bootstrap/reverse_tunnel/upstream_socket_interface/reverse_connection_io_handle.h"

#include "test/mocks/network/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {

class UpstreamReverseConnectionIOHandleTest : public testing::Test {
protected:
  UpstreamReverseConnectionIOHandleTest() {
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

TEST_F(UpstreamReverseConnectionIOHandleTest, ConnectReturnsSuccess) {
  auto address = Network::Utility::parseInternetAddressNoThrow("127.0.0.1", 8080);

  auto result = io_handle_->connect(address);

  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.errno_, 0);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, CloseCleansUpSocket) {
  auto result = io_handle_->close();

  EXPECT_EQ(result.err_, nullptr);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, GetSocketReturnsConstReference) {
  const auto& socket = io_handle_->getSocket();

  EXPECT_NE(&socket, nullptr);
}

TEST_F(UpstreamReverseConnectionIOHandleTest, ShutdownIgnoredWhenOwned) {
  auto result = io_handle_->shutdown(SHUT_RDWR);
  EXPECT_EQ(result.return_value_, 0);
  EXPECT_EQ(result.errno_, 0);
}

} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy
