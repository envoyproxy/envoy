#include "common/network/io_socket_error_impl.h"
#include "common/network/io_socket_handle_impl.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

TEST(IoSocketHandleImplTest, TestIoSocketError) {
  IoSocketError error1(EAGAIN);
  EXPECT_DEBUG_DEATH(error1.getErrorCode(),
                     ".*assert failure: .* Details: Didn't use getIoSocketEagainInstance.*");

  EXPECT_EQ(::strerror(EAGAIN), IoSocketError::getIoSocketEagainInstance()->getErrorDetails());

  IoSocketError error3(ENOTSUP);
  EXPECT_EQ(IoSocketError::IoErrorCode::NoSupport, error3.getErrorCode());
  EXPECT_EQ(::strerror(ENOTSUP), error3.getErrorDetails());

  IoSocketError error4(EAFNOSUPPORT);
  EXPECT_EQ(IoSocketError::IoErrorCode::AddressFamilyNoSupport, error4.getErrorCode());
  EXPECT_EQ(::strerror(EAFNOSUPPORT), error4.getErrorDetails());

  IoSocketError error5(EINPROGRESS);
  EXPECT_EQ(IoSocketError::IoErrorCode::InProgress, error5.getErrorCode());
  EXPECT_EQ(::strerror(EINPROGRESS), error5.getErrorDetails());

  IoSocketError error6(EPERM);
  EXPECT_EQ(IoSocketError::IoErrorCode::Permission, error6.getErrorCode());
  EXPECT_EQ(::strerror(EPERM), error6.getErrorDetails());

  // Random unknown error.
  IoSocketError error7(123);
  EXPECT_EQ(IoSocketError::IoErrorCode::UnknownError, error7.getErrorCode());
  EXPECT_EQ(::strerror(123), error7.getErrorDetails());
}

} // namespace
} // namespace Network
} // namespace Envoy
