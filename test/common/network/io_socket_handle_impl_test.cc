#include "common/network/io_socket_handle_impl.h"

#include "test/test_common/test_base.h"

namespace Envoy {
namespace Network {
namespace {

TEST(IoSocketHandleImplTest, TestIoSocketError) {
  IoSocketError error1(0);
  EXPECT_EQ(IoSocketError::IoErrorCode::NoError, error1.getErrorCode());
  EXPECT_EQ(::strerror(0), error1.getErrorDetails());

  IoSocketError error2(EAGAIN);
  EXPECT_EQ(IoSocketError::IoErrorCode::Again, error2.getErrorCode());
  EXPECT_EQ(::strerror(EAGAIN), error2.getErrorDetails());

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

  // Random unknow error.
  IoSocketError error7(123);
  EXPECT_EQ(IoSocketError::IoErrorCode::UnknownError, error7.getErrorCode());
  EXPECT_EQ(::strerror(123), error7.getErrorDetails());
}

}  // namespace
}  // namespace Network
}  // namespace Envoy
