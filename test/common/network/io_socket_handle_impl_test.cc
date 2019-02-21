#include "common/network/io_socket_handle_impl.h"

#include "test/test_common/test_base.h"

namespace Envoy {
namespace Network {
namespace {

TEST(IoSocketHandleImplTest, TestIoSocketError) {
  IoSocketError error1(EAGAIN);
  EXPECT_DEATH(IoError::getErrorCode(error1), "");

  EXPECT_EQ("Try again later", IoError::getErrorDetails(*ENVOY_ERROR_AGAIN));

  IoSocketError error3(ENOTSUP);
  EXPECT_EQ(IoSocketError::IoErrorCode::NoSupport, IoError::getErrorCode(error3));
  EXPECT_EQ(::strerror(ENOTSUP), IoError::getErrorDetails(error3));

  IoSocketError error4(EAFNOSUPPORT);
  EXPECT_EQ(IoSocketError::IoErrorCode::AddressFamilyNoSupport, IoError::getErrorCode(error4));
  EXPECT_EQ(::strerror(EAFNOSUPPORT), IoError::getErrorDetails(error4));

  IoSocketError error5(EINPROGRESS);
  EXPECT_EQ(IoSocketError::IoErrorCode::InProgress, IoError::getErrorCode(error5));
  EXPECT_EQ(::strerror(EINPROGRESS), IoError::getErrorDetails(error5));

  IoSocketError error6(EPERM);
  EXPECT_EQ(IoSocketError::IoErrorCode::Permission, IoError::getErrorCode(error6));
  EXPECT_EQ(::strerror(EPERM), IoError::getErrorDetails(error6));

  // Random unknown error.
  IoSocketError error7(123);
  EXPECT_EQ(IoSocketError::IoErrorCode::UnknownError, IoError::getErrorCode(error7));
  EXPECT_EQ(::strerror(123), IoError::getErrorDetails(error7));
}

} // namespace
} // namespace Network
} // namespace Envoy
