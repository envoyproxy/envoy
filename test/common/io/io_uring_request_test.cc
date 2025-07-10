#include "source/common/io/io_uring_worker_impl.h"

#include "test/mocks/io/mocks.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Io {
namespace {

class IoUringRequestTest : public ::testing::Test {
public:
  MockIoUringSocket mock_socket_;
};

TEST_F(IoUringRequestTest, SendRequestBasic) {
  std::string test_data = "Hello, Send!";

  SendRequest req(mock_socket_, test_data.c_str(), test_data.length(), MSG_NOSIGNAL);

  EXPECT_EQ(req.type(), Request::RequestType::Send);
  EXPECT_EQ(req.buf_, test_data.c_str());
  EXPECT_EQ(req.len_, test_data.length());
  EXPECT_EQ(req.flags_, MSG_NOSIGNAL);
  EXPECT_EQ(&req.socket(), &mock_socket_);
}

TEST_F(IoUringRequestTest, SendRequestEmptyData) {
  std::string empty_data = "";

  SendRequest req(mock_socket_, empty_data.c_str(), empty_data.length(), 0);

  EXPECT_EQ(req.type(), Request::RequestType::Send);
  EXPECT_EQ(req.buf_, empty_data.c_str());
  EXPECT_EQ(req.len_, 0);
  EXPECT_EQ(req.flags_, 0);
}

TEST_F(IoUringRequestTest, RecvRequestBasic) {
  size_t buffer_size = 1024;
  int flags = MSG_PEEK;

  RecvRequest req(mock_socket_, buffer_size, flags);

  EXPECT_EQ(req.type(), Request::RequestType::Recv);
  EXPECT_NE(req.buf_.get(), nullptr);
  EXPECT_EQ(req.len_, buffer_size);
  EXPECT_EQ(req.flags_, flags);
  EXPECT_EQ(&req.socket(), &mock_socket_);
}

TEST_F(IoUringRequestTest, RecvRequestZeroSize) {
  RecvRequest req(mock_socket_, 0, 0);

  EXPECT_EQ(req.type(), Request::RequestType::Recv);
  EXPECT_NE(req.buf_.get(), nullptr); // Should still allocate some space
  EXPECT_EQ(req.len_, 0);
  EXPECT_EQ(req.flags_, 0);
}

TEST_F(IoUringRequestTest, SendMsgRequestBasic) {
  std::string test_data = "Hello, Sendmsg!";
  struct iovec iov;
  iov.iov_base = const_cast<char*>(test_data.c_str());
  iov.iov_len = test_data.length();

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  SendMsgRequest req(mock_socket_, &msg, MSG_DONTWAIT);

  EXPECT_EQ(req.type(), Request::RequestType::SendMsg);
  EXPECT_NE(req.msg_.get(), nullptr);
  EXPECT_NE(req.iov_.get(), nullptr);
  EXPECT_EQ(req.flags_, MSG_DONTWAIT);
  EXPECT_EQ(&req.socket(), &mock_socket_);

  // Verify deep copy worked
  EXPECT_EQ(req.msg_->msg_iovlen, 1);
  EXPECT_EQ(req.msg_->msg_iov, req.iov_.get());
  EXPECT_EQ(req.iov_[0].iov_len, test_data.length());
}

TEST_F(IoUringRequestTest, SendMsgRequestWithName) {
  std::string test_data = "Hello, Sendmsg with name!";
  struct iovec iov;
  iov.iov_base = const_cast<char*>(test_data.c_str());
  iov.iov_len = test_data.length();

  struct sockaddr_in addr;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(8080);

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_name = &addr;
  msg.msg_namelen = sizeof(addr);
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;

  SendMsgRequest req(mock_socket_, &msg, 0);

  EXPECT_EQ(req.type(), Request::RequestType::SendMsg);
  EXPECT_NE(req.msg_.get(), nullptr);
  EXPECT_NE(req.iov_.get(), nullptr);
  EXPECT_NE(req.name_buf_.get(), nullptr);

  // Verify name was copied
  EXPECT_EQ(req.msg_->msg_namelen, sizeof(addr));
  EXPECT_EQ(req.msg_->msg_name, req.name_buf_.get());

  // Verify the address was copied correctly
  struct sockaddr_in* copied_addr = reinterpret_cast<struct sockaddr_in*>(req.name_buf_.get());
  EXPECT_EQ(copied_addr->sin_family, AF_INET);
  EXPECT_EQ(copied_addr->sin_port, htons(8080));
}

TEST_F(IoUringRequestTest, SendMsgRequestWithControl) {
  std::string test_data = "Hello, Sendmsg with control!";
  struct iovec iov;
  iov.iov_base = const_cast<char*>(test_data.c_str());
  iov.iov_len = test_data.length();

  char control_data[64];
  memset(control_data, 0x42, sizeof(control_data));

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = &iov;
  msg.msg_iovlen = 1;
  msg.msg_control = control_data;
  msg.msg_controllen = sizeof(control_data);

  SendMsgRequest req(mock_socket_, &msg, 0);

  EXPECT_EQ(req.type(), Request::RequestType::SendMsg);
  EXPECT_NE(req.msg_.get(), nullptr);
  EXPECT_NE(req.iov_.get(), nullptr);
  EXPECT_NE(req.control_buf_.get(), nullptr);

  // Verify control data was copied
  EXPECT_EQ(req.msg_->msg_controllen, sizeof(control_data));
  EXPECT_EQ(req.msg_->msg_control, req.control_buf_.get());

  // Verify the control data was copied correctly
  EXPECT_EQ(memcmp(req.control_buf_.get(), control_data, sizeof(control_data)), 0);
}

TEST_F(IoUringRequestTest, SendMsgRequestMultipleIovec) {
  std::string data1 = "First";
  std::string data2 = "Second";
  std::string data3 = "Third";

  struct iovec iov[3];
  iov[0].iov_base = const_cast<char*>(data1.c_str());
  iov[0].iov_len = data1.length();
  iov[1].iov_base = const_cast<char*>(data2.c_str());
  iov[1].iov_len = data2.length();
  iov[2].iov_base = const_cast<char*>(data3.c_str());
  iov[2].iov_len = data3.length();

  struct msghdr msg;
  memset(&msg, 0, sizeof(msg));
  msg.msg_iov = iov;
  msg.msg_iovlen = 3;

  SendMsgRequest req(mock_socket_, &msg, 0);

  EXPECT_EQ(req.type(), Request::RequestType::SendMsg);
  EXPECT_NE(req.msg_.get(), nullptr);
  EXPECT_NE(req.iov_.get(), nullptr);

  // Verify all iovecs were copied
  EXPECT_EQ(req.msg_->msg_iovlen, 3);
  EXPECT_EQ(req.msg_->msg_iov, req.iov_.get());

  for (int i = 0; i < 3; i++) {
    EXPECT_EQ(req.iov_[i].iov_base, iov[i].iov_base);
    EXPECT_EQ(req.iov_[i].iov_len, iov[i].iov_len);
  }
}

TEST_F(IoUringRequestTest, RecvMsgRequestBasic) {
  size_t buf_size = 1024;
  size_t control_size = 64;
  int flags = MSG_TRUNC;

  RecvMsgRequest req(mock_socket_, buf_size, control_size, flags);

  EXPECT_EQ(req.type(), Request::RequestType::RecvMsg);
  EXPECT_NE(req.msg_.get(), nullptr);
  EXPECT_NE(req.iov_.get(), nullptr);
  EXPECT_NE(req.buf_.get(), nullptr);
  EXPECT_NE(req.name_buf_.get(), nullptr);
  EXPECT_NE(req.control_buf_.get(), nullptr);
  EXPECT_EQ(req.buf_size_, buf_size);
  EXPECT_EQ(req.control_size_, control_size);
  EXPECT_EQ(req.flags_, flags);
  EXPECT_EQ(&req.socket(), &mock_socket_);

  // Verify msghdr is set up correctly
  EXPECT_EQ(req.msg_->msg_iov, req.iov_.get());
  EXPECT_EQ(req.msg_->msg_iovlen, 1);
  EXPECT_EQ(req.msg_->msg_name, req.name_buf_.get());
  EXPECT_EQ(req.msg_->msg_namelen, 256); // Standard sockaddr_storage size
  EXPECT_EQ(req.msg_->msg_control, req.control_buf_.get());
  EXPECT_EQ(req.msg_->msg_controllen, control_size);

  // Verify iovec is set up correctly
  EXPECT_EQ(req.iov_->iov_base, req.buf_.get());
  EXPECT_EQ(req.iov_->iov_len, buf_size);
}

TEST_F(IoUringRequestTest, RecvMsgRequestZeroSizes) {
  RecvMsgRequest req(mock_socket_, 0, 0, 0);

  EXPECT_EQ(req.type(), Request::RequestType::RecvMsg);
  EXPECT_NE(req.msg_.get(), nullptr);
  EXPECT_NE(req.iov_.get(), nullptr);
  EXPECT_NE(req.buf_.get(), nullptr); // Should still allocate
  EXPECT_NE(req.name_buf_.get(), nullptr);
  EXPECT_NE(req.control_buf_.get(), nullptr); // Should still allocate
  EXPECT_EQ(req.buf_size_, 0);
  EXPECT_EQ(req.control_size_, 0);
  EXPECT_EQ(req.flags_, 0);

  // Verify iovec is set up for zero-size buffer
  EXPECT_EQ(req.iov_->iov_base, req.buf_.get());
  EXPECT_EQ(req.iov_->iov_len, 0);
}

TEST_F(IoUringRequestTest, RequestTypeEnumValues) {
  // Verify the new request types have the expected enum values
  EXPECT_EQ(static_cast<uint8_t>(Request::RequestType::Send), 0x80);
  EXPECT_EQ(static_cast<uint8_t>(Request::RequestType::Recv), 0x100);
  EXPECT_EQ(static_cast<uint8_t>(Request::RequestType::SendMsg), 0x200);
  EXPECT_EQ(static_cast<uint8_t>(Request::RequestType::RecvMsg), 0x400);
}

// Test memory management and cleanup
TEST_F(IoUringRequestTest, RequestDestructors) {
  // Test that destructors properly clean up memory
  {
    SendRequest send_req(mock_socket_, "test", 4, 0);
    // No manual cleanup needed, destructor should handle it
  }

  {
    RecvRequest recv_req(mock_socket_, 1024, 0);
    // No manual cleanup needed, destructor should handle it
  }

  {
    std::string test_data = "test";
    struct iovec iov;
    iov.iov_base = const_cast<char*>(test_data.c_str());
    iov.iov_len = test_data.length();

    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;

    SendMsgRequest sendmsg_req(mock_socket_, &msg, 0);
    // No manual cleanup needed, destructor should handle it
  }

  {
    RecvMsgRequest recvmsg_req(mock_socket_, 1024, 64, 0);
    // No manual cleanup needed, destructor should handle it
  }

  // If we reach here without crashes, memory management is working correctly
  SUCCEED();
}

} // namespace
} // namespace Io
} // namespace Envoy
