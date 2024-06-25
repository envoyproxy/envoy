#include "source/common/network/io_socket_error_impl.h"
#include "source/common/tls/io_handle_bio.h"

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
  EXPECT_EQ(-1, BIO_write(bio_, nullptr, 10));
  const int err = ERR_get_error();
  EXPECT_EQ(ERR_GET_LIB(err), ERR_LIB_SYS);
  EXPECT_EQ(ERR_GET_REASON(err), 100);
}

TEST_F(IoHandleBioTest, TestMiscApis) {
  EXPECT_EQ(BIO_read(bio_, nullptr, 0), 0);

  int ret = BIO_reset(bio_);
  EXPECT_EQ(ret, 0);

  ret = BIO_flush(bio_);
  EXPECT_EQ(ret, 1);
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
