#include "source/common/io/io_uring_worker_impl.h"

#include "source/common/api/os_sys_calls_impl.h"

#include "gtest/gtest.h"
#include <sys/socket.h>

namespace Envoy {
namespace Io {
namespace {

class IoUringWorkerIntegraionTest : public testing::Test {
public:
    void init() {
      server_socket_ = Api::OsSysCallsSingleton::get().socket(AF_INET, SOCK_STREAM, IPPROTO_TCP).return_value_;
      EXPECT_TRUE(SOCKET_VALID(server_socket_));
      struct sockaddr_in server_addr;
      memset(&server_addr, 0, sizeof(server_addr));
      server_addr.sin_family = AF_INET;
      server_addr.sin_port = 0;
      EXPECT_EQ(inet_pton(AF_INET, "127.0.0.1", &server_addr.sin_addr.s_addr), 1);
      EXPECT_EQ(Api::OsSysCallsSingleton::get().bind(server_socket_,
                reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)).return_value_, 0);
      EXPECT_EQ(Api::OsSysCallsSingleton::get().listen(server_socket_, 5).return_value_, 0);

      // Using non blocking for the client socket, then we won't block on the test thread.
      client_socket_ = Api::OsSysCallsSingleton::get().socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP).return_value_;
      EXPECT_TRUE(SOCKET_VALID(client_socket_));
    }

    uint32_t getServerPort() {
      struct sockaddr_in sin;
      socklen_t len = sizeof(sin);
      EXPECT_EQ(getsockname(server_socket_, reinterpret_cast<struct sockaddr*>(&sin), &len), 0);
      return sin.sin_port;
    }
  
    os_fd_t server_socket_;
    os_fd_t client_socket_;
};

TEST_F(IoUringWorkerIntegraionTest, basic) {
  init();
  ENVOY_LOG_MISC(debug, "the server port {}", getServerPort());
  
}

} // namespace
} // namespace Io
} // namespace Envoy