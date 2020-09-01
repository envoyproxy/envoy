#include "common/network/io_socket_error_impl.h"

#include "extensions/transport_sockets/tls/io_handle_bio.h"

#include "test/mocks/network/io_handle.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/ssl.h"

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

TEST_F(IoHandleBioTest, TestMiscApis) {
  EXPECT_EQ(bio_->method->destroy(nullptr), 0);
  EXPECT_EQ(bio_->method->bread(nullptr, nullptr, 0), 0);

  int ret;
  NiceMock<Network::MockIoHandle>* ptr;

  ret = bio_->method->ctrl(bio_, BIO_C_GET_FD, 0, &ptr);
  EXPECT_EQ(ret, -1);
  EXPECT_EQ(ptr, &io_handle_);

  ret = bio_->method->ctrl(bio_, BIO_CTRL_RESET, 0, nullptr);
  EXPECT_EQ(ret, 0);

  ret = bio_->method->ctrl(bio_, BIO_CTRL_SET_CLOSE, 1, nullptr);
  EXPECT_EQ(ret, 1);

  ret = bio_->method->ctrl(bio_, BIO_CTRL_GET_CLOSE, 0, nullptr);
  EXPECT_EQ(ret, 1);

  // avoid BIO_free assert in destructor
  ret = bio_->method->ctrl(bio_, BIO_CTRL_SET_CLOSE, 0, nullptr);
  EXPECT_EQ(ret, 1);

  EXPECT_CALL(io_handle_, close())
      .WillOnce(Return(testing::ByMove(Api::IoCallUint64Result{
          0, Api::IoErrorPtr(nullptr, Network::IoSocketError::deleteIoError)})));
  bio_->init = 1;
  bio_->shutdown = 1;
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
