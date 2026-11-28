#include <cerrno>
#include <utility>
#include <vector>

#include "envoy/api/io_error.h"
#include "envoy/api/os_sys_calls_common.h"
#include "envoy/buffer/buffer.h"
#include "envoy/common/platform.h"
#include "envoy/network/io_handle.h"

#include "source/common/api/os_sys_calls_impl.h"
#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/win32_socket_handle_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "absl/container/fixed_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Network {

class Win32SocketHandleImplTest : public testing::Test {
public:
  Win32SocketHandleImplTest() : io_handle_(42) {
    dispatcher_ = std::make_unique<NiceMock<Event::MockDispatcher>>();
    file_event_ = new NiceMock<Event::MockFileEvent>;
    EXPECT_CALL(*dispatcher_, createFileEvent_(42, _, _, _)).WillOnce(Return(file_event_));
    io_handle_.setBlocking(false);
    io_handle_.initializeFileEvent(
        *dispatcher_, [](uint32_t) { return absl::OkStatus(); }, Event::PlatformDefaultTriggerType,
        Event::FileReadyType::Read | Event::FileReadyType::Closed);
  }

protected:
  std::unique_ptr<NiceMock<Event::MockDispatcher>> dispatcher_;
  NiceMock<Event::MockFileEvent>* file_event_;
  Network::Win32SocketHandleImpl io_handle_;
};

TEST_F(Win32SocketHandleImplTest, ReadvWithNoBufferShouldReadFromTheWire) {

  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(1)
      .WillRepeatedly(Return(Api::SysCallSizeResult{10, 0}));

  Buffer::OwnedImpl read_buffer;
  Buffer::Reservation reservation = read_buffer.reserveForRead();
  auto rc = io_handle_.readv(reservation.length(), reservation.slices(), reservation.numSlices());
  EXPECT_EQ(rc.return_value_, 10);
}

TEST_F(Win32SocketHandleImplTest, ReadvShouldReenableEventsOnBlock) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(1)
      .WillRepeatedly(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_AGAIN}));

  EXPECT_CALL(*file_event_, registerEventIfEmulatedEdge(_));
  Buffer::OwnedImpl read_buffer;
  Buffer::Reservation reservation = read_buffer.reserveForRead();
  auto rc = io_handle_.readv(reservation.length(), reservation.slices(), reservation.numSlices());
  EXPECT_EQ(rc.return_value_, 0);
  EXPECT_EQ(rc.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
}

TEST_F(Win32SocketHandleImplTest, ReadvWithBufferShouldReadFromBuffer) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  constexpr int data_length = 10;
  std::string data(data_length, '*');

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _)).WillOnce([&](os_fd_t, void* buffer, size_t, int) {
    memcpy(buffer, data.data(), data_length); // NOLINT(safe-memcpy)
    return Api::SysCallSizeResult{data_length, 0};
  });

  absl::FixedArray<char> buf(data_length);
  auto rc = io_handle_.recv(buf.data(), buf.size(), MSG_PEEK);
  EXPECT_EQ(rc.return_value_, data_length);
  EXPECT_EQ(data, std::string(buf.data(), buf.size()));
  Buffer::OwnedImpl read_buffer;
  Buffer::Reservation reservation = read_buffer.reserveForRead();
  rc = io_handle_.readv(reservation.length(), reservation.slices(), reservation.numSlices());
  EXPECT_EQ(rc.return_value_, 10);
  reservation.commit(rc.return_value_);
  EXPECT_EQ(data, read_buffer.toString());
}

TEST_F(Win32SocketHandleImplTest, RecvWithoutPeekShouldReadFromWire) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .Times(1)
      .WillRepeatedly(Return(Api::SysCallSizeResult{10, 0}));

  absl::FixedArray<char> buf(10);
  auto rc = io_handle_.recv(buf.data(), buf.size(), 0);
  EXPECT_EQ(rc.return_value_, 10);
}

TEST_F(Win32SocketHandleImplTest, RecvWithPeekMultipleTimes) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .WillOnce([&](os_fd_t, void*, size_t length, int) {
        EXPECT_EQ(10, length);
        return Api::SysCallSizeResult{5, 0};
      })
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_AGAIN}));

  EXPECT_CALL(*file_event_, registerEventIfEmulatedEdge(_));
  absl::FixedArray<char> buf(10);
  auto rc = io_handle_.recv(buf.data(), buf.size(), MSG_PEEK);
  EXPECT_EQ(rc.return_value_, 5);
  EXPECT_CALL(os_sys_calls, recv(_, _, _, _)).WillOnce([&](os_fd_t, void*, size_t length, int) {
    EXPECT_EQ(5, length);
    return Api::SysCallSizeResult{5, 0};
  });

  auto rc2 = io_handle_.recv(buf.data(), buf.size(), MSG_PEEK);
  EXPECT_EQ(rc2.return_value_, 10);
}

TEST_F(Win32SocketHandleImplTest, RecvWithPeekReactivatesReadOnBlock) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .Times(1)
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_AGAIN}));

  EXPECT_CALL(*file_event_, registerEventIfEmulatedEdge(_));
  absl::FixedArray<char> buf(10);
  auto rc = io_handle_.recv(buf.data(), buf.size(), MSG_PEEK);
  EXPECT_EQ(rc.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
}

TEST_F(Win32SocketHandleImplTest, RecvWithPeekFlagReturnsFinalError) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  constexpr int data_length = 10;

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _))
      .Times(2)
      .WillOnce(
          [&](os_fd_t, void*, size_t, int) { return Api::SysCallSizeResult{data_length / 2, 0}; })
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_CONNRESET}));

  absl::FixedArray<char> buf(data_length);
  auto rc = io_handle_.recv(buf.data(), buf.size(), MSG_PEEK);
  EXPECT_EQ(rc.err_->getErrorCode(), Api::IoError::IoErrorCode::ConnectionReset);
}

TEST_F(Win32SocketHandleImplTest, ReadvWithPeekShouldReadFromBuffer) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  constexpr int data_length = 10;
  std::string data(data_length, '*');

  EXPECT_CALL(os_sys_calls, recv(_, _, _, _)).WillOnce([&](os_fd_t, void* buffer, size_t, int) {
    memcpy(buffer, data.data(), data_length); // NOLINT(safe-memcpy)
    return Api::SysCallSizeResult{data_length, 0};
  });

  absl::FixedArray<char> buf(data_length);
  auto rc = io_handle_.recv(buf.data(), buf.size(), MSG_PEEK);
  EXPECT_EQ(rc.return_value_, data_length);
  EXPECT_EQ(data, std::string(buf.data(), buf.size()));
  // Second call should not make a system call, it should
  // read from memory.
  rc = io_handle_.recv(buf.data(), buf.size(), MSG_PEEK);
  EXPECT_EQ(rc.return_value_, data_length);
  EXPECT_EQ(data, std::string(buf.data(), buf.size()));
}

TEST_F(Win32SocketHandleImplTest, RecvMsgShouldReenableEventsOnConnectionReset) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_CONNRESET}));
  EXPECT_CALL(*file_event_, registerEventIfEmulatedEdge(Event::FileReadyType::Read));

  char buf[16] = {};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  IoHandle::RecvMsgOutput output(1, nullptr);
  auto rc = io_handle_.recvmsg(&slice, 1, 1234, IoHandle::UdpSaveCmsgConfig(), output);
  EXPECT_FALSE(rc.ok());
  EXPECT_EQ(rc.err_->getErrorCode(), Api::IoError::IoErrorCode::ConnectionReset);
}

TEST_F(Win32SocketHandleImplTest, RecvMsgShouldReenableEventsOnAllMappedTransientErrors) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  const std::vector<std::pair<int, Api::IoError::IoErrorCode>> transient_errors = {
      {SOCKET_ERROR_AGAIN, Api::IoError::IoErrorCode::Again},
      {SOCKET_ERROR_CONNRESET, Api::IoError::IoErrorCode::ConnectionReset},
      {SOCKET_ERROR_INTR, Api::IoError::IoErrorCode::Interrupt},
      {SOCKET_ERROR_MSG_SIZE, Api::IoError::IoErrorCode::MessageTooBig},
      {SOCKET_ERROR_NETUNREACH, Api::IoError::IoErrorCode::NetworkUnreachable},
      {SOCKET_ERROR_NOBUFS, Api::IoError::IoErrorCode::NoBufferSpace},
  };

  for (const auto& [sys_err, expected_code] : transient_errors) {
    EXPECT_CALL(os_sys_calls, recvmsg(_, _, _))
        .WillOnce(Return(Api::SysCallSizeResult{-1, sys_err}));
    EXPECT_CALL(*file_event_, registerEventIfEmulatedEdge(Event::FileReadyType::Read));

    char buf[16] = {};
    Buffer::RawSlice slice{buf, sizeof(buf)};
    IoHandle::RecvMsgOutput output(1, nullptr);
    auto rc = io_handle_.recvmsg(&slice, 1, 1234, IoHandle::UdpSaveCmsgConfig(), output);
    EXPECT_FALSE(rc.ok());
    EXPECT_EQ(rc.err_->getErrorCode(), expected_code);
  }
}

TEST_F(Win32SocketHandleImplTest, RecvMsgShouldReenableEventsOnTransientUnknownSystemErrors) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

#ifdef WIN32
  const std::vector<int> transient_sys_errors = {WSAENETRESET, WSAEHOSTUNREACH, WSAECONNREFUSED,
                                                 WSAENOBUFS};
#else
  const std::vector<int> transient_sys_errors = {ENETRESET, EHOSTUNREACH, ECONNREFUSED};
#endif

  for (int err : transient_sys_errors) {
    EXPECT_CALL(os_sys_calls, recvmsg(_, _, _)).WillOnce(Return(Api::SysCallSizeResult{-1, err}));
    EXPECT_CALL(*file_event_, registerEventIfEmulatedEdge(Event::FileReadyType::Read));

    char buf[16] = {};
    Buffer::RawSlice slice{buf, sizeof(buf)};
    IoHandle::RecvMsgOutput output(1, nullptr);
    auto rc = io_handle_.recvmsg(&slice, 1, 1234, IoHandle::UdpSaveCmsgConfig(), output);
    EXPECT_FALSE(rc.ok());
    EXPECT_EQ(rc.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
    EXPECT_EQ(rc.err_->getSystemErrorCode(), err);
  }
}

TEST_F(Win32SocketHandleImplTest, RecvMsgShouldNotReenableEventsOnBadFd) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_BADF}));
  EXPECT_CALL(*file_event_, registerEventIfEmulatedEdge(_)).Times(0);

  char buf[16] = {};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  IoHandle::RecvMsgOutput output(1, nullptr);
  auto rc = io_handle_.recvmsg(&slice, 1, 1234, IoHandle::UdpSaveCmsgConfig(), output);
  EXPECT_FALSE(rc.ok());
  EXPECT_EQ(rc.err_->getErrorCode(), Api::IoError::IoErrorCode::BadFd);
}

TEST_F(Win32SocketHandleImplTest, RecvMsgShouldNotReenableEventsOnNonTransientErrors) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recvmsg(_, _, _))
      .WillOnce(Return(Api::SysCallSizeResult{-1, SOCKET_ERROR_INVAL}))
      .WillOnce(Return(Api::SysCallSizeResult{-1, EFAULT}));
  EXPECT_CALL(*file_event_, registerEventIfEmulatedEdge(_)).Times(0);

  char buf[16] = {};
  Buffer::RawSlice slice{buf, sizeof(buf)};
  IoHandle::RecvMsgOutput output(1, nullptr);
  auto rc1 = io_handle_.recvmsg(&slice, 1, 1234, IoHandle::UdpSaveCmsgConfig(), output);
  EXPECT_FALSE(rc1.ok());
  EXPECT_EQ(rc1.err_->getErrorCode(), Api::IoError::IoErrorCode::InvalidArgument);

  auto rc2 = io_handle_.recvmsg(&slice, 1, 1234, IoHandle::UdpSaveCmsgConfig(), output);
  EXPECT_FALSE(rc2.ok());
  EXPECT_EQ(rc2.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
}

TEST_F(Win32SocketHandleImplTest, RecvMmsgShouldReenableEventsOnTransientError) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  const std::vector<std::pair<int, Api::IoError::IoErrorCode>> transient_errors = {
      {SOCKET_ERROR_AGAIN, Api::IoError::IoErrorCode::Again},
      {SOCKET_ERROR_CONNRESET, Api::IoError::IoErrorCode::ConnectionReset},
      {SOCKET_ERROR_INTR, Api::IoError::IoErrorCode::Interrupt},
      {SOCKET_ERROR_MSG_SIZE, Api::IoError::IoErrorCode::MessageTooBig},
      {SOCKET_ERROR_NETUNREACH, Api::IoError::IoErrorCode::NetworkUnreachable},
      {SOCKET_ERROR_NOBUFS, Api::IoError::IoErrorCode::NoBufferSpace},
  };

  for (const auto& [sys_err, expected_code] : transient_errors) {
    EXPECT_CALL(os_sys_calls, recvmmsg(_, _, _, _, _))
        .WillOnce(Return(Api::SysCallIntResult{-1, sys_err}));
    EXPECT_CALL(*file_event_, registerEventIfEmulatedEdge(Event::FileReadyType::Read));

    char buf[16] = {};
    RawSliceArrays slices(1, absl::FixedArray<Buffer::RawSlice>{{buf, sizeof(buf)}});
    IoHandle::RecvMsgOutput output(1, nullptr);
    auto rc = io_handle_.recvmmsg(slices, 1234, IoHandle::UdpSaveCmsgConfig(), output);
    EXPECT_FALSE(rc.ok());
    EXPECT_EQ(rc.err_->getErrorCode(), expected_code);
  }
}

TEST_F(Win32SocketHandleImplTest, RecvMmsgShouldNotReenableEventsOnNonTransientError) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);

  EXPECT_CALL(os_sys_calls, recvmmsg(_, _, _, _, _))
      .WillOnce(Return(Api::SysCallIntResult{-1, SOCKET_ERROR_INVAL}));
  EXPECT_CALL(*file_event_, registerEventIfEmulatedEdge(_)).Times(0);

  char buf[16] = {};
  RawSliceArrays slices(1, absl::FixedArray<Buffer::RawSlice>{{buf, sizeof(buf)}});
  IoHandle::RecvMsgOutput output(1, nullptr);
  auto rc = io_handle_.recvmmsg(slices, 1234, IoHandle::UdpSaveCmsgConfig(), output);
  EXPECT_FALSE(rc.ok());
  EXPECT_EQ(rc.err_->getErrorCode(), Api::IoError::IoErrorCode::InvalidArgument);
}

} // namespace Network
} // namespace Envoy
