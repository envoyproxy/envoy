#include "common/network/io_socket_handle_impl.h"

#include "test/test_common/test_base.h"

namespace Envoy {
namespace Network {
namespace {

TEST(IoSocketHandleImplTest, TestIoSocketError) {
  IoSocketError error1(EAGAIN);
  EXPECT_DEATH(Api::IoError::getErrorCode(error1), "");

  EXPECT_EQ("Try again later", Api::IoError::getErrorDetails(*ENVOY_ERROR_AGAIN));

  IoSocketError error3(ENOTSUP);
  EXPECT_EQ(IoSocketError::IoErrorCode::NoSupport, Api::IoError::getErrorCode(error3));
  EXPECT_EQ(::strerror(ENOTSUP), Api::IoError::getErrorDetails(error3));

  IoSocketError error4(EAFNOSUPPORT);
  EXPECT_EQ(IoSocketError::IoErrorCode::AddressFamilyNoSupport, Api::IoError::getErrorCode(error4));
  EXPECT_EQ(::strerror(EAFNOSUPPORT), Api::IoError::getErrorDetails(error4));

  IoSocketError error5(EINPROGRESS);
  EXPECT_EQ(IoSocketError::IoErrorCode::InProgress, Api::IoError::getErrorCode(error5));
  EXPECT_EQ(::strerror(EINPROGRESS), Api::IoError::getErrorDetails(error5));

  IoSocketError error6(EPERM);
  EXPECT_EQ(IoSocketError::IoErrorCode::Permission, Api::IoError::getErrorCode(error6));
  EXPECT_EQ(::strerror(EPERM), Api::IoError::getErrorDetails(error6));

  // Random unknown error.
  IoSocketError error7(123);
  EXPECT_EQ(IoSocketError::IoErrorCode::UnknownError, Api::IoError::getErrorCode(error7));
  EXPECT_EQ(::strerror(123), Api::IoError::getErrorDetails(error7));
}

} // namespace
} // namespace Network
} // namespace Envoy
