#include "common/network/buffered_io_socket_handle_impl.h"

#include "absl/container/fixed_array.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include <sys/socket.h>

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;

namespace Envoy {
namespace Network {
namespace {

class BufferedIoSocketHandleTest : public testing::Test {
public:
  BufferedIoSocketHandleTest() : buf_(1024) {
    io_handle_ = std::make_unique<Network::BufferedIoSocketHandleImpl>();
    io_handle_peer_ = std::make_unique<Network::BufferedIoSocketHandleImpl>();
    io_handle_->setWritablePeer(io_handle_peer_.get());
    io_handle_peer_->setWritablePeer(io_handle_.get());
  }
  ~BufferedIoSocketHandleTest() override {
    io_handle_->close();
    io_handle_peer_->close();
  }
  void expectAgain() {
    auto res = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(Api::IoError::IoErrorCode::Again, res.err_->getErrorCode());
  }
  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_;
  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_peer_;
  absl::FixedArray<char> buf_;
};

// Test recv side effects.
TEST_F(BufferedIoSocketHandleTest, TestBasicRecv) {
  auto res = io_handle_->recv(buf_.data(), buf_.size(), 0);
  // EAGAIN.
  EXPECT_FALSE(res.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, res.err_->getErrorCode());
  io_handle_->setWriteEnd();
  res = io_handle_->recv(buf_.data(), buf_.size(), 0);
  EXPECT_FALSE(res.ok());
  EXPECT_NE(Api::IoError::IoErrorCode::Again, res.err_->getErrorCode());
}

// Test recv side effects.
TEST_F(BufferedIoSocketHandleTest, TestBasicPeek) {
  auto res = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
  // EAGAIN.
  EXPECT_FALSE(res.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, res.err_->getErrorCode());
  io_handle_->setWriteEnd();
  res = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
  EXPECT_FALSE(res.ok());
  EXPECT_NE(Api::IoError::IoErrorCode::Again, res.err_->getErrorCode());
}

TEST_F(BufferedIoSocketHandleTest, TestRecvDrain) {
  auto& internal_buffer = io_handle_->getBufferForTest();
  internal_buffer.add("abcd");
  auto res = io_handle_->recv(buf_.data(), buf_.size(), 0);
  EXPECT_TRUE(res.ok());
  EXPECT_EQ(4, res.rc_);
  EXPECT_EQ(absl::string_view(buf_.data(), 4), "abcd");
  EXPECT_EQ(0, internal_buffer.length());
  expectAgain();
}

// This test fixture test that io handle is impacted by peer.
class BufferedIoSocketHandlePeerTest : public testing::Test {
public:
  BufferedIoSocketHandlePeerTest() : buf_(1024) {
    io_handle_ = std::make_unique<Network::BufferedIoSocketHandleImpl>();
    io_handle_peer_ = std::make_unique<Network::BufferedIoSocketHandleImpl>();
    io_handle_->setWritablePeer(io_handle_peer_.get());
    io_handle_peer_->setWritablePeer(io_handle_.get());
  }
  ~BufferedIoSocketHandlePeerTest() override {
    io_handle_->close();
    io_handle_peer_->close();
  }
  void expectAgain() {
    auto res = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(Api::IoError::IoErrorCode::Again, res.err_->getErrorCode());
  }
  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_;
  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_peer_;
  absl::FixedArray<char> buf_;
};

TEST_F(BufferedIoSocketHandlePeerTest, TestRecvDrain) {}

} // namespace
} // namespace Network
} // namespace Envoy