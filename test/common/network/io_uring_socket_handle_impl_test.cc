#include "source/common/network/address_impl.h"
#include "source/common/network/io_uring_socket_handle_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/io/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

namespace Envoy {
namespace Network {
namespace {

class IoUringSocketHandleTestImpl : public IoUringSocketHandleImpl {
public:
  IoUringSocketHandleTestImpl(Io::IoUringWorkerFactory& factory, bool is_server_socket)
      : IoUringSocketHandleImpl(factory, INVALID_SOCKET, false, std::nullopt, is_server_socket) {}
  IoUringSocketType ioUringSocketType() const { return io_uring_socket_type_; }
};

class IoUringSocketHandleTest : public ::testing::Test {
public:
  Io::MockIoUringSocket socket_;
  Io::MockIoUringWorker worker_;
  Io::MockIoUringWorkerFactory factory_;
  Event::MockDispatcher dispatcher_;
};

TEST_F(IoUringSocketHandleTest, CreateServerSocket) {
  IoUringSocketHandleTestImpl impl(factory_, true);
  EXPECT_EQ(IoUringSocketType::Server, impl.ioUringSocketType());
}

TEST_F(IoUringSocketHandleTest, CreateClientSocket) {
  IoUringSocketHandleTestImpl impl(factory_, false);
  EXPECT_EQ(IoUringSocketType::Unknown, impl.ioUringSocketType());
  EXPECT_CALL(worker_, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket_));
  EXPECT_CALL(factory_, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker_)));
  impl.initializeFileEvent(
      dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);
  EXPECT_EQ(IoUringSocketType::Client, impl.ioUringSocketType());
}

TEST_F(IoUringSocketHandleTest, ReadError) {
  IoUringSocketHandleTestImpl impl(factory_, false);
  EXPECT_CALL(worker_, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket_));
  EXPECT_CALL(factory_, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker_)));
  impl.initializeFileEvent(
      dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);

  // EAGAIN error.
  Buffer::OwnedImpl read_buffer;
  Io::ReadParam read_param{read_buffer, -EAGAIN};
  auto read_param_ref = OptRef<Io::ReadParam>(read_param);
  EXPECT_CALL(socket_, getReadParam()).WillOnce(testing::ReturnRef(read_param_ref));
  auto ret = impl.read(read_buffer, std::nullopt);
  EXPECT_EQ(ret.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);

  // Non-EAGAIN error.
  Io::ReadParam read_param_2{read_buffer, -EBADF};
  auto read_param_ref_2 = OptRef<Io::ReadParam>(read_param_2);
  EXPECT_CALL(socket_, getReadParam()).WillOnce(testing::ReturnRef(read_param_ref_2));
  ret = impl.read(read_buffer, std::nullopt);
  EXPECT_EQ(ret.err_->getErrorCode(), Api::IoError::IoErrorCode::BadFd);
}

TEST_F(IoUringSocketHandleTest, WriteError) {
  IoUringSocketHandleTestImpl impl(factory_, false);
  EXPECT_CALL(worker_, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket_));
  EXPECT_CALL(factory_, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker_)));
  impl.initializeFileEvent(
      dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);

  Buffer::OwnedImpl write_buffer;
  Io::WriteParam write_param{-EBADF};
  auto write_param_ref = OptRef<Io::WriteParam>(write_param);
  EXPECT_CALL(socket_, getWriteParam()).WillOnce(testing::ReturnRef(write_param_ref));
  auto ret = impl.write(write_buffer);
  EXPECT_EQ(ret.err_->getErrorCode(), Api::IoError::IoErrorCode::BadFd);
}

TEST_F(IoUringSocketHandleTest, WritevError) {
  IoUringSocketHandleTestImpl impl(factory_, false);
  EXPECT_CALL(worker_, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket_));
  EXPECT_CALL(factory_, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker_)));
  impl.initializeFileEvent(
      dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);

  Buffer::OwnedImpl write_buffer;
  Io::WriteParam write_param{-EBADF};
  auto write_param_ref = OptRef<Io::WriteParam>(write_param);
  EXPECT_CALL(socket_, getWriteParam()).WillOnce(testing::ReturnRef(write_param_ref));
  auto slice = write_buffer.frontSlice();
  auto ret = impl.writev(&slice, 1);
  EXPECT_EQ(ret.err_->getErrorCode(), Api::IoError::IoErrorCode::BadFd);
}

TEST_F(IoUringSocketHandleTest, WriteBackpressureReturnsEagain) {
  IoUringSocketHandleTestImpl impl(factory_, false);
  EXPECT_CALL(worker_, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket_));
  EXPECT_CALL(factory_, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker_)));
  impl.initializeFileEvent(
      dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);

  // No pending write error, so the write proceeds to the socket.
  OptRef<Io::WriteParam> no_write_param;
  EXPECT_CALL(socket_, getWriteParam()).WillOnce(testing::ReturnRef(no_write_param));
  // The socket is applying backpressure and accepts nothing, so the data stays in the buffer and
  // the handle reports EAGAIN.
  Buffer::OwnedImpl write_buffer;
  write_buffer.add("hello");
  EXPECT_CALL(socket_, write(testing::An<Buffer::Instance&>()));
  auto ret = impl.write(write_buffer);
  EXPECT_EQ(ret.return_value_, 0);
  EXPECT_EQ(ret.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
  EXPECT_EQ(5, write_buffer.length());
}

TEST_F(IoUringSocketHandleTest, WritevBackpressureReturnsEagain) {
  IoUringSocketHandleTestImpl impl(factory_, false);
  EXPECT_CALL(worker_, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket_));
  EXPECT_CALL(factory_, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker_)));
  impl.initializeFileEvent(
      dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);

  // No pending write error, so the write proceeds to the socket.
  OptRef<Io::WriteParam> no_write_param;
  EXPECT_CALL(socket_, getWriteParam()).WillOnce(testing::ReturnRef(no_write_param));
  // The socket accepts nothing while `backpressured`, so the handle reports EAGAIN.
  Buffer::OwnedImpl write_buffer;
  write_buffer.add("hello");
  auto slice = write_buffer.frontSlice();
  EXPECT_CALL(socket_, write(testing::An<const Buffer::RawSlice*>(), 1))
      .WillOnce(testing::Return(0));
  auto ret = impl.writev(&slice, 1);
  EXPECT_EQ(ret.return_value_, 0);
  EXPECT_EQ(ret.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
}

// A client socket reports its connect result only through the write event that delivers it.
// Outside that event, for example when a TLS read probes for a reset after the peer closed,
// SO_ERROR must come from the kernel.
TEST_F(IoUringSocketHandleTest, ClientSocketErrorOutsideWriteEventComesFromKernel) {
  testing::NiceMock<Envoy::Api::MockOsSysCalls> os_sys_calls;
  Envoy::TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  IoUringSocketHandleTestImpl impl(factory_, false);
  EXPECT_CALL(worker_, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket_));
  EXPECT_CALL(factory_, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker_)));
  impl.initializeFileEvent(
      dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);

  OptRef<Io::WriteParam> no_write_param;
  EXPECT_CALL(socket_, getWriteParam()).WillRepeatedly(testing::ReturnRef(no_write_param));
  EXPECT_CALL(os_sys_calls, getsockopt_(_, SOL_SOCKET, SO_ERROR, _, _))
      .WillOnce(testing::Return(0));
  int error = -1;
  socklen_t error_size = sizeof(error);
  EXPECT_EQ(0, impl.getOption(SOL_SOCKET, SO_ERROR, &error, &error_size).return_value_);
  EXPECT_EQ(0, error);
}

// resetFileEvents() must detach the handler callback, matching the epoll handle which destroys its
// file event. Otherwise a completion after a reset would call into an owner that is being torn
// down.
TEST_F(IoUringSocketHandleTest, ResetFileEventsDetachesCallback) {
  IoUringSocketHandleTestImpl impl(factory_, true);
  EXPECT_CALL(worker_, addServerSocket(_, _, _)).WillOnce(testing::ReturnRef(socket_));
  EXPECT_CALL(factory_, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker_)));
  impl.initializeFileEvent(
      dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);

  EXPECT_CALL(socket_, disableRead());
  EXPECT_CALL(socket_, enableCloseEvent(false));
  // The callback is replaced with a no-op so a later completion cannot reach the released owner.
  EXPECT_CALL(socket_, setFileReadyCb(_));
  impl.resetFileEvents();
}

// During worker shutdown the io_uring worker thread is no longer registered, but the owning
// IoUringServerSocket still closes the fd through `IORING_OP_CLOSE`. The handle destructor must not
// also raw close the fd, which could hit a number already reused by another worker's accept.
TEST_F(IoUringSocketHandleTest, DestructorDoesNotRawCloseFdOwnedByIoUringSocket) {
  const os_fd_t fd = ::socket(AF_INET, SOCK_STREAM, 0);
  ASSERT_GE(fd, 0);
  {
    IoUringSocketHandleImpl impl(factory_, fd, false, std::nullopt, true);
    EXPECT_CALL(worker_, addServerSocket(_, _, _)).WillOnce(testing::ReturnRef(socket_));
    EXPECT_CALL(factory_, getIoUringWorker())
        .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker_)));
    impl.initializeFileEvent(
        dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
        Event::FileReadyType::Read);

    // The worker thread is being torn down, so the io_uring socket, not the handle, owns the fd.
    EXPECT_CALL(factory_, currentThreadRegistered()).WillRepeatedly(testing::Return(false));
  }
  // The handle did not close the fd, so it is still open here.
  EXPECT_EQ(0, fcntl(fd, F_GETFD));
  ::close(fd);
}

TEST_F(IoUringSocketHandleTest, SendmsgNotSupported) {
  IoUringSocketHandleTestImpl impl(factory_, true);

  Buffer::OwnedImpl write_buffer;
  auto slice = write_buffer.frontSlice();
  auto local_addr = std::make_shared<Network::Address::Ipv4Instance>("127.0.0.1", 0);
  EXPECT_THAT(impl.sendmsg(&slice, 0, 0, nullptr, *local_addr).err_->getErrorCode(),
              Api::IoError::IoErrorCode::NoSupport);
}

TEST_F(IoUringSocketHandleTest, RecvmsgNotSupported) {
  IoUringSocketHandleTestImpl impl(factory_, true);

  Buffer::OwnedImpl write_buffer;
  auto slice = write_buffer.frontSlice();
  IoHandle::RecvMsgOutput output(0, nullptr);
  EXPECT_THAT(impl.recvmsg(&slice, 0, 0, {}, output).err_->getErrorCode(),
              Api::IoError::IoErrorCode::NoSupport);
}

TEST_F(IoUringSocketHandleTest, RecvmmsgNotSupported) {
  IoUringSocketHandleTestImpl impl(factory_, true);

  Buffer::OwnedImpl write_buffer;
  RawSliceArrays array(0, absl::FixedArray<Buffer::RawSlice>(0));
  IoHandle::RecvMsgOutput output(0, nullptr);
  EXPECT_THAT(impl.recvmmsg(array, 0, {}, output).err_->getErrorCode(),
              Api::IoError::IoErrorCode::NoSupport);
}

TEST_F(IoUringSocketHandleTest, CloseWithSendRstTrueSetsLingerZeroAndCloses) {
  testing::NiceMock<Envoy::Api::MockOsSysCalls> os_sys_calls;
  auto os_calls =
      std::make_unique<Envoy::TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl>>(
          &os_sys_calls);

  os_fd_t test_fd = 42;
  IoUringSocketHandleImpl io_handle(factory_, test_fd, false, std::nullopt, false);

  EXPECT_CALL(os_sys_calls,
              setsockopt_(test_fd, SOL_SOCKET, SO_LINGER, testing::_, sizeof(struct linger)))
      .WillOnce(testing::Invoke([](os_fd_t, int, int, const void* optval, socklen_t) -> int {
        const auto* l = static_cast<const struct linger*>(optval);
        EXPECT_EQ(1, l->l_onoff);
        EXPECT_EQ(0, l->l_linger);
        return 0;
      }));

  io_handle.setAbortiveClose();
  auto res = io_handle.close();
  EXPECT_EQ(0, res.return_value_);
  EXPECT_FALSE(io_handle.isOpen());
}

} // namespace
} // namespace Network
} // namespace Envoy
