#include "common/network/io_socket_error_impl.h"
#include "common/network/io_socket_handle_impl.h"

#include "test/test_common/test_base.h"

namespace Envoy {
namespace Network {
namespace {

TEST(IoSocketHandleImplTest, TestIoSocketError) {
  IoSocketError error1(EAGAIN);
  EXPECT_DEATH(error1.getErrorCode(), "Didn't use IoSocketEagain to represent EAGAIN");

  EXPECT_EQ(::strerror(EAGAIN), getIoSocketEagainInstance()->getErrorDetails());

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
