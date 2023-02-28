#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/io/io_uring_factory_impl.h"
#include "source/common/io/io_uring_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/io_uring_socket_handle_impl.h"
#include "source/common/thread_local/thread_local_impl.h"

#include "test/test_common/test_time.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

class IoUringSocketHandleImplIntegrationTest : public testing::Test {
public:
  IoUringSocketHandleImplIntegrationTest() : should_skip_(!Io::isIoUringSupported()) {}

  void SetUp() override {
    if (should_skip_) {
      GTEST_SKIP();
    }
  }

  void TearDown() override {
    instance_.shutdownGlobalThreading();
    instance_.shutdownThread();
  }

  void initialize() {
    api_ = Api::createApiForTest(time_system_);
    dispatcher_ = api_->allocateDispatcher("test_thread");
    instance_.registerThread(*dispatcher_, true);

    io_uring_factory_ = std::make_unique<Io::IoUringFactoryImpl>(10, false, instance_);
    io_uring_factory_->onServerInitialized();
    fd_ = Api::OsSysCallsSingleton::get().socket(AF_INET, SOCK_STREAM, IPPROTO_TCP).return_value_;
    EXPECT_GE(fd_, 0);
    io_handle_ = std::make_unique<IoUringSocketHandleImpl>(*io_uring_factory_, fd_);

    os_fd_t fd = Api::OsSysCallsSingleton::get()
                     .socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, IPPROTO_TCP)
                     .return_value_;
    EXPECT_GE(fd, 0);
    peer_io_handle_ = std::make_unique<IoSocketHandleImpl>(fd);
  }

  bool should_skip_{false};
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  Event::GlobalTimeSystem time_system_;
  ThreadLocal::InstanceImpl instance_;
  std::unique_ptr<Io::IoUringFactory> io_uring_factory_;
  os_fd_t fd_;
  IoHandlePtr io_handle_;
  IoHandlePtr peer_io_handle_;
};

TEST_F(IoUringSocketHandleImplIntegrationTest, Close) {
  initialize();

  io_handle_->close();

  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(errno, EBADF);
}

TEST_F(IoUringSocketHandleImplIntegrationTest, CancelAndClose) {
  initialize();

  // Submit accept requests with listening.
  io_handle_->listen(5);
  io_handle_->initializeFileEvent(
      *dispatcher_, [](uint32_t) {}, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  io_handle_->close();

  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_EQ(errno, EBADF);
}

TEST_F(IoUringSocketHandleImplIntegrationTest, Accept) {
  initialize();

  // io_uring handle starts listening.
  bool accepted = false;
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  io_handle_->bind(local_addr);
  io_handle_->listen(5);
  io_handle_->initializeFileEvent(
      *dispatcher_,
      [this, &accepted](uint32_t) {
        struct sockaddr addr;
        socklen_t addrlen = sizeof(addr);
        auto handle = io_handle_->accept(&addr, &addrlen);
        EXPECT_NE(handle, nullptr);
        accepted = true;
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // Connect from peer handle.
  peer_io_handle_->connect(io_handle_->localAddress());

  while (!accepted) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  EXPECT_TRUE(accepted);

  // Close safely.
  io_handle_->close();
  while (fcntl(fd_, F_GETFD, 0) >= 0) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

} // namespace
} // namespace Network
} // namespace Envoy
