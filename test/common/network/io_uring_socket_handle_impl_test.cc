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
      : IoUringSocketHandleImpl(factory, INVALID_SOCKET, false, absl::nullopt, is_server_socket) {}
  IoUringSocketType ioUringSocketType() const { return io_uring_socket_type_; }
};

TEST(IoUringSocketHandleImpl, CreateServerSocket) {
  Io::MockIoUringWorkerFactory factory;
  IoUringSocketHandleTestImpl impl(factory, true);
  EXPECT_EQ(IoUringSocketType::Server, impl.ioUringSocketType());
}

TEST(IoUringSocketHandleImpl, CreateClientSocket) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Io::MockIoUringSocket socket;
  Io::MockIoUringWorker worker;
  Io::MockIoUringWorkerFactory factory;
  Event::MockDispatcher dispatcher;
  IoUringSocketHandleTestImpl impl(factory, false);
  EXPECT_EQ(IoUringSocketType::Unknown, impl.ioUringSocketType());
  EXPECT_CALL(worker, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket));
  EXPECT_CALL(factory, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker)));
  impl.initializeFileEvent(
      dispatcher, [](uint32_t) {}, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  EXPECT_EQ(IoUringSocketType::Client, impl.ioUringSocketType());
}

TEST(IoUringSocketHandleImpl, ReadError) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Io::MockIoUringSocket socket;
  Io::MockIoUringWorker worker;
  Io::MockIoUringWorkerFactory factory;
  Event::MockDispatcher dispatcher;
  IoUringSocketHandleTestImpl impl(factory, false);
  EXPECT_EQ(IoUringSocketType::Unknown, impl.ioUringSocketType());
  EXPECT_CALL(worker, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket));
  EXPECT_CALL(factory, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker)));
  impl.initializeFileEvent(
      dispatcher, [](uint32_t) {}, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  // EAGAIN error.
  Buffer::OwnedImpl read_buffer;
  Io::ReadParam read_param{read_buffer, -EAGAIN};
  auto read_param_ref = OptRef<Io::ReadParam>(read_param);
  EXPECT_CALL(socket, getReadParam()).WillOnce(testing::ReturnRef(read_param_ref));
  auto ret = impl.read(read_buffer, absl::nullopt);
  EXPECT_EQ(ret.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);

  // Non-EAGAIN error.
  Io::ReadParam read_param_2{read_buffer, -EBADF};
  auto read_param_ref_2 = OptRef<Io::ReadParam>(read_param_2);
  EXPECT_CALL(socket, getReadParam()).WillOnce(testing::ReturnRef(read_param_ref_2));
  ret = impl.read(read_buffer, absl::nullopt);
  EXPECT_EQ(ret.err_->getErrorCode(), Api::IoError::IoErrorCode::BadFd);
}

TEST(IoUringSocketHandleImpl, WriteError) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Io::MockIoUringSocket socket;
  Io::MockIoUringWorker worker;
  Io::MockIoUringWorkerFactory factory;
  Event::MockDispatcher dispatcher;
  IoUringSocketHandleTestImpl impl(factory, false);
  EXPECT_EQ(IoUringSocketType::Unknown, impl.ioUringSocketType());
  EXPECT_CALL(worker, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket));
  EXPECT_CALL(factory, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker)));
  impl.initializeFileEvent(
      dispatcher, [](uint32_t) {}, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  Buffer::OwnedImpl write_buffer;
  Io::WriteParam write_param{-EBADF};
  auto write_param_ref = OptRef<Io::WriteParam>(write_param);
  EXPECT_CALL(socket, getWriteParam()).WillOnce(testing::ReturnRef(write_param_ref));
  auto ret = impl.write(write_buffer);
  EXPECT_EQ(ret.err_->getErrorCode(), Api::IoError::IoErrorCode::BadFd);
}

TEST(IoUringSocketHandleImpl, WritevError) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  Io::MockIoUringSocket socket;
  Io::MockIoUringWorker worker;
  Io::MockIoUringWorkerFactory factory;
  Event::MockDispatcher dispatcher;
  IoUringSocketHandleTestImpl impl(factory, false);
  EXPECT_EQ(IoUringSocketType::Unknown, impl.ioUringSocketType());
  EXPECT_CALL(worker, addClientSocket(_, _, _)).WillOnce(testing::ReturnRef(socket));
  EXPECT_CALL(factory, getIoUringWorker())
      .WillOnce(testing::Return(OptRef<Io::IoUringWorker>(worker)));
  impl.initializeFileEvent(
      dispatcher, [](uint32_t) {}, Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  Buffer::OwnedImpl write_buffer;
  Io::WriteParam write_param{-EBADF};
  auto write_param_ref = OptRef<Io::WriteParam>(write_param);
  EXPECT_CALL(socket, getWriteParam()).WillOnce(testing::ReturnRef(write_param_ref));
  auto slice = write_buffer.frontSlice();
  auto ret = impl.writev(&slice, 1);
  EXPECT_EQ(ret.err_->getErrorCode(), Api::IoError::IoErrorCode::BadFd);
}

} // namespace
} // namespace Network
} // namespace Envoy
