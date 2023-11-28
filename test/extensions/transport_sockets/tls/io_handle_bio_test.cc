#include "source/common/network/io_socket_error_impl.h"
#include "source/extensions/transport_sockets/tls/io_handle_bio.h"

#include "test/mocks/network/io_handle.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

using testing::_;
using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

class IoHandleBioTest : public testing::Test {
public:
  IoHandleBioTest() { bio_ = BIO_new_io_handle(&io_handle_); }
  ~IoHandleBioTest() override { BIO_free(bio_); }

  BIO* bio_;
  NiceMock<Network::MockIoHandle> io_handle_;
};

TEST_F(IoHandleBioTest, WriteError) {
  EXPECT_CALL(io_handle_, writev(_, 1))
      .WillOnce(
          Return(testing::ByMove(Api::IoCallUint64Result(0, Network::IoSocketError::create(100)))));
  EXPECT_EQ(-1, bio_->method->bwrite(bio_, nullptr, 10));
  const int err = ERR_get_error();
  EXPECT_EQ(ERR_GET_LIB(err), ERR_LIB_SYS);
  EXPECT_EQ(ERR_GET_REASON(err), 100);
}

TEST_F(IoHandleBioTest, TestMiscApis) {
  EXPECT_EQ(bio_->method->destroy(nullptr), 0);
  EXPECT_EQ(bio_->method->bread(nullptr, nullptr, 0), 0);

  EXPECT_DEATH(bio_->method->ctrl(bio_, BIO_C_GET_FD, 0, nullptr), "should not be called");
  EXPECT_DEATH(bio_->method->ctrl(bio_, BIO_C_SET_FD, 0, nullptr), "should not be called");

  int ret = bio_->method->ctrl(bio_, BIO_CTRL_RESET, 0, nullptr);
  EXPECT_EQ(ret, 0);

  ret = bio_->method->ctrl(bio_, BIO_CTRL_FLUSH, 0, nullptr);
  EXPECT_EQ(ret, 1);

  ret = bio_->method->ctrl(bio_, BIO_CTRL_SET_CLOSE, 1, nullptr);
  EXPECT_EQ(ret, 1);

  ret = bio_->method->ctrl(bio_, BIO_CTRL_GET_CLOSE, 0, nullptr);
  EXPECT_EQ(ret, 1);

  EXPECT_CALL(io_handle_, close())
      .WillOnce(Return(testing::ByMove(Api::IoCallUint64Result{0, Api::IoError::none()})));
  bio_->init = 1;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
