#include "common/common/utility.h"
#include "common/network/io_socket_error_impl.h"
#include "common/network/io_socket_handle_impl.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

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

} // namespace
} // namespace Network
} // namespace Envoy
