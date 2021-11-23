#include "source/common/common/utility.h"
#include "source/common/network/address_impl.h"
#include "source/common/network/io_socket_error_impl.h"
#include "source/common/network/io_socket_handle_impl.h"
#include "source/common/network/listen_socket_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Invoke;
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
        *dispatcher_, [](uint32_t) -> void {}, Event::PlatformDefaultTriggerType,
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
  absl::FixedArray<iovec> peek_iov(1);
  peek_iov[0].iov_base = static_cast<void*>(data.data());
  peek_iov[0].iov_len = data.length();
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .WillOnce(Invoke([&](os_fd_t, const iovec* iov, int num_iov) {
        // Gcc treats the variables as unused and this causes
        // a compilation failure.
        UNREFERENCED_PARAMETER(iov);
        UNREFERENCED_PARAMETER(num_iov);
        iov = peek_iov.begin();
        num_iov = 1;
        return Api::SysCallSizeResult{data_length, 0};
      }));

  absl::FixedArray<char> buf(data_length);
  auto rc = io_handle_.recv(buf.data(), buf.size(), MSG_PEEK);
  EXPECT_EQ(rc.return_value_, data_length);
  Buffer::OwnedImpl read_buffer;
  Buffer::Reservation reservation = read_buffer.reserveForRead();
  rc = io_handle_.readv(reservation.length(), reservation.slices(), reservation.numSlices());
  EXPECT_EQ(rc.return_value_, 10);
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

TEST_F(Win32SocketHandleImplTest, RecvWithPeekReactivatesReadOnBlock) {
  Api::MockOsSysCalls os_sys_calls;
  TestThreadsafeSingletonInjector<Api::OsSysCallsImpl> os_calls(&os_sys_calls);
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
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
  std::string data(data_length, '*');
  absl::FixedArray<iovec> peek_iov(1);
  peek_iov[0].iov_base = static_cast<void*>(data.data());
  peek_iov[0].iov_len = data.length();
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .Times(2)
      .WillOnce(Invoke([&](os_fd_t, const iovec* iov, int num_iov) {
        // Gcc treats the variables as unused and this causes
        // a compilation failure.
        UNREFERENCED_PARAMETER(iov);
        UNREFERENCED_PARAMETER(num_iov);
        iov = peek_iov.begin();
        num_iov = 1;
        return Api::SysCallSizeResult{data_length / 2, 0};
      }))
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
  absl::FixedArray<iovec> peek_iov(1);
  peek_iov[0].iov_base = static_cast<void*>(data.data());
  peek_iov[0].iov_len = data.length();
  EXPECT_CALL(os_sys_calls, readv(_, _, _))
      .WillOnce(Invoke([&](os_fd_t, const iovec* iov, int num_iov) {
        // Gcc treats the variables as unused and this causes
        // a compilation failure.
        UNREFERENCED_PARAMETER(iov);
        UNREFERENCED_PARAMETER(num_iov);
        iov = peek_iov.begin();
        num_iov = 1;
        return Api::SysCallSizeResult{data_length, 0};
      }));

  absl::FixedArray<char> buf(data_length);
  auto rc = io_handle_.recv(buf.data(), buf.size(), MSG_PEEK);
  EXPECT_EQ(rc.return_value_, data_length);
  // Second call should not make a system call, it should
  // read from memory.
  rc = io_handle_.recv(buf.data(), buf.size(), MSG_PEEK);
  EXPECT_EQ(rc.return_value_, data_length);
}

} // namespace Network
} // namespace Envoy
