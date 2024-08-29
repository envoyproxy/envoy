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
      : IoUringSocketHandleImpl(factory, INVALID_SOCKET, false, absl::nullopt, is_server_socket) {}
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
  auto ret = impl.read(read_buffer, absl::nullopt);
  EXPECT_EQ(ret.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);

  // Non-EAGAIN error.
  Io::ReadParam read_param_2{read_buffer, -EBADF};
  auto read_param_ref_2 = OptRef<Io::ReadParam>(read_param_2);
  EXPECT_CALL(socket_, getReadParam()).WillOnce(testing::ReturnRef(read_param_ref_2));
  ret = impl.read(read_buffer, absl::nullopt);
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

} // namespace
} // namespace Network
} // namespace Envoy
