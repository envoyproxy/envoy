#include "common/common/utility.h"
#include "common/network/address_impl.h"
#include "common/network/io_socket_error_impl.h"
#include "common/network/io_socket_handle_impl.h"
#include "common/network/listen_socket_impl.h"

#include "test/mocks/api/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/threadsafe_singleton_injector.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::DoAll;
using testing::Eq;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Network {
namespace {

TEST(IoSocketHandleImplTest, TestIoSocketError) {
  IoSocketError error1(SOCKET_ERROR_AGAIN);
  EXPECT_DEBUG_DEATH(error1.getErrorCode(),
                     ".*assert failure: .* Details: Didn't use getIoSocketEagainInstance.*");
  EXPECT_EQ(errorDetails(SOCKET_ERROR_AGAIN),
            IoSocketError::getIoSocketEagainInstance()->getErrorDetails());

  IoSocketError error2(SOCKET_ERROR_NOT_SUP);
  EXPECT_EQ(IoSocketError::IoErrorCode::NoSupport, error2.getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_NOT_SUP), error2.getErrorDetails());

  IoSocketError error3(SOCKET_ERROR_AF_NO_SUP);
  EXPECT_EQ(IoSocketError::IoErrorCode::AddressFamilyNoSupport, error3.getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_AF_NO_SUP), error3.getErrorDetails());

  IoSocketError error4(SOCKET_ERROR_IN_PROGRESS);
  EXPECT_EQ(IoSocketError::IoErrorCode::InProgress, error4.getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_IN_PROGRESS), error4.getErrorDetails());

  IoSocketError error5(SOCKET_ERROR_PERM);
  EXPECT_EQ(IoSocketError::IoErrorCode::Permission, error5.getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_PERM), error5.getErrorDetails());

  IoSocketError error6(SOCKET_ERROR_MSG_SIZE);
  EXPECT_EQ(IoSocketError::IoErrorCode::MessageTooBig, error6.getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_MSG_SIZE), error6.getErrorDetails());

  IoSocketError error7(SOCKET_ERROR_INTR);
  EXPECT_EQ(IoSocketError::IoErrorCode::Interrupt, error7.getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_INTR), error7.getErrorDetails());

  IoSocketError error8(SOCKET_ERROR_ADDR_NOT_AVAIL);
  EXPECT_EQ(IoSocketError::IoErrorCode::AddressNotAvailable, error8.getErrorCode());
  EXPECT_EQ(errorDetails(SOCKET_ERROR_ADDR_NOT_AVAIL), error8.getErrorDetails());

  // Random unknown error
  IoSocketError error9(123);
  EXPECT_EQ(IoSocketError::IoErrorCode::UnknownError, error9.getErrorCode());
  EXPECT_EQ(errorDetails(123), error9.getErrorDetails());
}

TEST(IoSocketHandleImpl, LastRoundTripTimeReturnsEmptyOptionalIfGetSocketFails) {
  NiceMock<Envoy::Api::MockOsSysCalls> os_sys_calls;
  auto os_calls =
      std::make_unique<Envoy::TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl>>(
          &os_sys_calls);

  EXPECT_CALL(os_sys_calls, socketTcpInfo(_, _))
      .WillOnce(Return(Api::SysCallBoolResult{false, -1}));

  IoSocketHandleImpl io_handle;
  EXPECT_THAT(io_handle.lastRoundTripTime(), Eq(absl::optional<std::chrono::microseconds>{}));
}

TEST(IoSocketHandleImpl, LastRoundTripTimeReturnsRttIfSuccessful) {
  NiceMock<Envoy::Api::MockOsSysCalls> os_sys_calls;
  auto rtt = std::chrono::microseconds(35);
  auto os_calls =
      std::make_unique<Envoy::TestThreadsafeSingletonInjector<Envoy::Api::OsSysCallsImpl>>(
          &os_sys_calls);

  EXPECT_CALL(os_sys_calls, socketTcpInfo(_, _))
      .WillOnce(
          Invoke([rtt](os_fd_t /*sockfd*/, Api::EnvoyTcpInfo* tcp_info) -> Api::SysCallBoolResult {
            tcp_info->tcpi_rtt = rtt;
            return {true, 0};
          }));

  IoSocketHandleImpl io_handle;
  EXPECT_THAT(io_handle.lastRoundTripTime(),
              Eq(std::chrono::duration_cast<std::chrono::milliseconds>(rtt)));
}
} // namespace
} // namespace Network
} // namespace Envoy
