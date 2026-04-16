#include "source/common/network/io_socket_error_impl.h"
#include "source/common/tls/io_handle_bio.h"

#include "test/mocks/network/io_handle.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "openssl/bio.h"
#include "openssl/err.h"
#include "openssl/ssl.h"

using testing::_;
using testing::Invoke;
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

static Api::IoCallUint64Result makeSuccessResult(uint64_t rc) {
  return {rc, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
}

static Api::IoCallUint64Result makeAgainResult() {
  return {0, Network::IoSocketError::getIoSocketEagainError()};
}

static Api::IoCallUint64Result makeErrorResult(int sys_errno) {
  return {0, Network::IoSocketError::create(sys_errno)};
}

TEST_F(IoHandleBioTest, Reset) { EXPECT_EQ(0, BIO_reset(bio_)); }

TEST_F(IoHandleBioTest, BioIsInitialized) {
  // The BIO should be initialized and ready for I/O.
  EXPECT_EQ(1, BIO_get_init(bio_));
}

TEST_F(IoHandleBioTest, ReadNullBuffer) {
  // Reading with a null output buffer should return 0 without calling readv.
  EXPECT_CALL(io_handle_, readv(_, _, _)).Times(0);
  EXPECT_EQ(0, BIO_read(bio_, nullptr, 10));
}

TEST_F(IoHandleBioTest, ReadSuccess) {
  char buf[16];
  EXPECT_CALL(io_handle_, readv(5, _, 1))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memcpy(slices[0].mem_, "hello", 5);
        return makeSuccessResult(5);
      }));
  EXPECT_EQ(5, BIO_read(bio_, buf, 5));
  EXPECT_EQ(std::string(buf, 5), "hello");
  // After a successful read, retry flags should be clear.
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, ReadPartial) {
  // readv returns fewer bytes than requested.
  char buf[16];
  EXPECT_CALL(io_handle_, readv(10, _, 1)).WillOnce(Return(testing::ByMove(makeSuccessResult(3))));
  EXPECT_EQ(3, BIO_read(bio_, buf, 10));
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, ReadEagain) {
  char buf[16];
  EXPECT_CALL(io_handle_, readv(10, _, 1)).WillOnce(Return(testing::ByMove(makeAgainResult())));
  EXPECT_EQ(-1, BIO_read(bio_, buf, 10));
  EXPECT_TRUE(BIO_should_retry(bio_));
  EXPECT_TRUE(BIO_should_read(bio_));
}

TEST_F(IoHandleBioTest, ReadError) {
  char buf[16];
  EXPECT_CALL(io_handle_, readv(10, _, 1)).WillOnce(Return(testing::ByMove(makeErrorResult(42))));
  EXPECT_EQ(-1, BIO_read(bio_, buf, 10));
  EXPECT_FALSE(BIO_should_retry(bio_));
  const int err = ERR_get_error();
  EXPECT_EQ(ERR_GET_LIB(err), ERR_LIB_SYS);
  EXPECT_EQ(ERR_GET_REASON(err), 42);
}

TEST_F(IoHandleBioTest, ReadEof) {
  // readv returning 0 bytes means EOF.
  char buf[16];
  EXPECT_CALL(io_handle_, readv(10, _, 1)).WillOnce(Return(testing::ByMove(makeSuccessResult(0))));
  EXPECT_EQ(0, BIO_read(bio_, buf, 10));
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, ReadClearsRetryFlags) {
  char buf[16];
  // First read: EAGAIN sets retry flags.
  EXPECT_CALL(io_handle_, readv(10, _, 1)).WillOnce(Return(testing::ByMove(makeAgainResult())));
  EXPECT_EQ(-1, BIO_read(bio_, buf, 10));
  EXPECT_TRUE(BIO_should_retry(bio_));

  // Second read: success should clear retry flags.
  EXPECT_CALL(io_handle_, readv(10, _, 1)).WillOnce(Return(testing::ByMove(makeSuccessResult(5))));
  EXPECT_EQ(5, BIO_read(bio_, buf, 10));
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, ReadInterrupt) {
  char buf[16];
  // Interrupt (EINTR) should also set retry flag, same as EAGAIN.
  EXPECT_CALL(io_handle_, readv(10, _, 1))
      .WillOnce(Return(testing::ByMove(makeErrorResult(SOCKET_ERROR_INTR))));
  EXPECT_EQ(-1, BIO_read(bio_, buf, 10));
  EXPECT_TRUE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, ReadOneByte) {
  char buf[1];
  EXPECT_CALL(io_handle_, readv(1, _, 1))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        EXPECT_EQ(1, slices[0].len_);
        *static_cast<char*>(slices[0].mem_) = 'X';
        return makeSuccessResult(1);
      }));
  EXPECT_EQ(1, BIO_read(bio_, buf, 1));
  EXPECT_EQ('X', buf[0]);
}

TEST_F(IoHandleBioTest, ReadSliceSetup) {
  // Verify that the buffer pointer and length are correctly passed through to readv.
  char buf[64];
  EXPECT_CALL(io_handle_, readv(64, _, 1))
      .WillOnce(Invoke([&buf](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        EXPECT_EQ(buf, slices[0].mem_);
        EXPECT_EQ(64, slices[0].len_);
        return makeSuccessResult(10);
      }));
  EXPECT_EQ(10, BIO_read(bio_, buf, 64));
}

TEST_F(IoHandleBioTest, ReadErrorDoesNotSetRetry) {
  // A non-retryable error (not EAGAIN or EINTR) must NOT set retry flags.
  char buf[16];
  EXPECT_CALL(io_handle_, readv(10, _, 1))
      .WillOnce(Return(testing::ByMove(makeErrorResult(ECONNREFUSED))));
  EXPECT_EQ(-1, BIO_read(bio_, buf, 10));
  EXPECT_FALSE(BIO_should_retry(bio_));
  EXPECT_FALSE(BIO_should_read(bio_));
  ERR_clear_error();
}

TEST_F(IoHandleBioTest, WriteSuccess) {
  const char data[] = "hello";
  EXPECT_CALL(io_handle_, writev(_, 1)).WillOnce(Return(testing::ByMove(makeSuccessResult(5))));
  EXPECT_EQ(5, BIO_write(bio_, data, 5));
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, WritePartial) {
  const char data[] = "hello world";
  EXPECT_CALL(io_handle_, writev(_, 1)).WillOnce(Return(testing::ByMove(makeSuccessResult(5))));
  EXPECT_EQ(5, BIO_write(bio_, data, 11));
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, WriteEagain) {
  const char data[] = "hello";
  EXPECT_CALL(io_handle_, writev(_, 1)).WillOnce(Return(testing::ByMove(makeAgainResult())));
  EXPECT_EQ(-1, BIO_write(bio_, data, 5));
  EXPECT_TRUE(BIO_should_retry(bio_));
  EXPECT_TRUE(BIO_should_write(bio_));
}

TEST_F(IoHandleBioTest, WriteInterrupt) {
  const char data[] = "hello";
  // Interrupt (EINTR) on write should also set retry flag.
  EXPECT_CALL(io_handle_, writev(_, 1))
      .WillOnce(Return(testing::ByMove(makeErrorResult(SOCKET_ERROR_INTR))));
  EXPECT_EQ(-1, BIO_write(bio_, data, 5));
  EXPECT_TRUE(BIO_should_retry(bio_));
  EXPECT_TRUE(BIO_should_write(bio_));
}

TEST_F(IoHandleBioTest, WriteErrorDoesNotSetRetry) {
  // A non-retryable write error must NOT set retry flags.
  EXPECT_CALL(io_handle_, writev(_, 1))
      .WillOnce(Return(testing::ByMove(makeErrorResult(ECONNREFUSED))));
  EXPECT_EQ(-1, BIO_write(bio_, "x", 1));
  EXPECT_FALSE(BIO_should_retry(bio_));
  EXPECT_FALSE(BIO_should_write(bio_));
  ERR_clear_error();
}

TEST_F(IoHandleBioTest, WriteOneByte) {
  EXPECT_CALL(io_handle_, writev(_, 1))
      .WillOnce(Invoke([](const Buffer::RawSlice* slices, uint64_t) {
        EXPECT_EQ(1, slices[0].len_);
        EXPECT_EQ('Z', *static_cast<const char*>(slices[0].mem_));
        return makeSuccessResult(1);
      }));
  EXPECT_EQ(1, BIO_write(bio_, "Z", 1));
}

TEST_F(IoHandleBioTest, WriteSliceSetup) {
  // Verify the data pointer and length are correctly passed through to writev.
  const char data[] = "test data";
  EXPECT_CALL(io_handle_, writev(_, 1))
      .WillOnce(Invoke([&data](const Buffer::RawSlice* slices, uint64_t) {
        EXPECT_EQ(data, slices[0].mem_);
        EXPECT_EQ(9, slices[0].len_);
        return makeSuccessResult(9);
      }));
  EXPECT_EQ(9, BIO_write(bio_, data, 9));
}

TEST_F(IoHandleBioTest, WriteClearsRetryFlags) {
  const char data[] = "hello";
  // First write: EAGAIN.
  EXPECT_CALL(io_handle_, writev(_, 1)).WillOnce(Return(testing::ByMove(makeAgainResult())));
  EXPECT_EQ(-1, BIO_write(bio_, data, 5));
  EXPECT_TRUE(BIO_should_retry(bio_));

  // Second write: success should clear retry flags.
  EXPECT_CALL(io_handle_, writev(_, 1)).WillOnce(Return(testing::ByMove(makeSuccessResult(5))));
  EXPECT_EQ(5, BIO_write(bio_, data, 5));
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, ReadThenWrite) {
  char buf[16];
  const char data[] = "world";

  // Read some data.
  EXPECT_CALL(io_handle_, readv(5, _, 1))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memcpy(slices[0].mem_, "hello", 5);
        return makeSuccessResult(5);
      }));
  EXPECT_EQ(5, BIO_read(bio_, buf, 5));

  // Write some data.
  EXPECT_CALL(io_handle_, writev(_, 1)).WillOnce(Return(testing::ByMove(makeSuccessResult(5))));
  EXPECT_EQ(5, BIO_write(bio_, data, 5));
}

TEST_F(IoHandleBioTest, MultipleReads) {
  char buf[16];
  EXPECT_CALL(io_handle_, readv(_, _, 1))
      .WillOnce(Return(testing::ByMove(makeSuccessResult(3))))
      .WillOnce(Return(testing::ByMove(makeAgainResult())))
      .WillOnce(Return(testing::ByMove(makeSuccessResult(7))));

  EXPECT_EQ(3, BIO_read(bio_, buf, 10));
  EXPECT_FALSE(BIO_should_retry(bio_));

  EXPECT_EQ(-1, BIO_read(bio_, buf, 10));
  EXPECT_TRUE(BIO_should_retry(bio_));

  EXPECT_EQ(7, BIO_read(bio_, buf, 10));
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, MultipleWrites) {
  const char data[] = "hello";
  EXPECT_CALL(io_handle_, writev(_, 1))
      .WillOnce(Return(testing::ByMove(makeSuccessResult(5))))
      .WillOnce(Return(testing::ByMove(makeAgainResult())))
      .WillOnce(Return(testing::ByMove(makeSuccessResult(5))));

  EXPECT_EQ(5, BIO_write(bio_, data, 5));
  EXPECT_FALSE(BIO_should_retry(bio_));

  EXPECT_EQ(-1, BIO_write(bio_, data, 5));
  EXPECT_TRUE(BIO_should_retry(bio_));

  EXPECT_EQ(5, BIO_write(bio_, data, 5));
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, ReadErrorCodeIsRecoverable) {
  char buf[16];
  // After an EAGAIN, subsequent successful read should work fine.
  EXPECT_CALL(io_handle_, readv(10, _, 1))
      .WillOnce(Return(testing::ByMove(makeAgainResult())))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memcpy(slices[0].mem_, "data", 4);
        return makeSuccessResult(4);
      }));

  EXPECT_EQ(-1, BIO_read(bio_, buf, 10));
  EXPECT_TRUE(BIO_should_retry(bio_));

  EXPECT_EQ(4, BIO_read(bio_, buf, 10));
  EXPECT_FALSE(BIO_should_retry(bio_));
  EXPECT_EQ(std::string(buf, 4), "data");
}

TEST_F(IoHandleBioTest, WriteErrorSetsErrno) {
  // Different errno values should be preserved.
  EXPECT_CALL(io_handle_, writev(_, 1)).WillOnce(Return(testing::ByMove(makeErrorResult(EPERM))));
  EXPECT_EQ(-1, BIO_write(bio_, "x", 1));
  const int err = ERR_get_error();
  EXPECT_EQ(ERR_GET_LIB(err), ERR_LIB_SYS);
  EXPECT_EQ(ERR_GET_REASON(err), EPERM);
}

TEST_F(IoHandleBioTest, ReadErrorSetsErrno) {
  char buf[16];
  EXPECT_CALL(io_handle_, readv(1, _, 1))
      .WillOnce(Return(testing::ByMove(makeErrorResult(ECONNRESET))));
  EXPECT_EQ(-1, BIO_read(bio_, buf, 1));
  const int err = ERR_get_error();
  EXPECT_EQ(ERR_GET_LIB(err), ERR_LIB_SYS);
  EXPECT_EQ(ERR_GET_REASON(err), ECONNRESET);
}

TEST_F(IoHandleBioTest, ReadEagainDoesNotSetError) {
  // EAGAIN should set retry flags but should NOT push an error onto the error stack.
  char buf[16];
  EXPECT_CALL(io_handle_, readv(10, _, 1)).WillOnce(Return(testing::ByMove(makeAgainResult())));
  EXPECT_EQ(-1, BIO_read(bio_, buf, 10));
  EXPECT_TRUE(BIO_should_retry(bio_));
  EXPECT_EQ(0u, ERR_peek_error());
}

TEST_F(IoHandleBioTest, WriteEagainDoesNotSetError) {
  // EAGAIN should set retry flags but should NOT push an error onto the error stack.
  EXPECT_CALL(io_handle_, writev(_, 1)).WillOnce(Return(testing::ByMove(makeAgainResult())));
  EXPECT_EQ(-1, BIO_write(bio_, "x", 1));
  EXPECT_TRUE(BIO_should_retry(bio_));
  EXPECT_EQ(0u, ERR_peek_error());
}

TEST_F(IoHandleBioTest, ConsecutiveErrors) {
  // Multiple errors should each push their own entry onto the error stack.
  char buf[16];
  EXPECT_CALL(io_handle_, readv(10, _, 1))
      .WillOnce(Return(testing::ByMove(makeErrorResult(ECONNRESET))))
      .WillOnce(Return(testing::ByMove(makeErrorResult(EPIPE))));

  EXPECT_EQ(-1, BIO_read(bio_, buf, 10));
  EXPECT_EQ(-1, BIO_read(bio_, buf, 10));

  // Both errors should be on the stack (FIFO order).
  const int err1 = ERR_get_error();
  EXPECT_EQ(ERR_GET_REASON(err1), ECONNRESET);
  const int err2 = ERR_get_error();
  EXPECT_EQ(ERR_GET_REASON(err2), EPIPE);
}

TEST_F(IoHandleBioTest, WriteThenReadInterleaved) {
  const char wdata[] = "req";
  char rbuf[16];

  // Write a request.
  EXPECT_CALL(io_handle_, writev(_, 1)).WillOnce(Return(testing::ByMove(makeSuccessResult(3))));
  EXPECT_EQ(3, BIO_write(bio_, wdata, 3));
  EXPECT_FALSE(BIO_should_retry(bio_));

  // Read a response.
  EXPECT_CALL(io_handle_, readv(16, _, 1))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memcpy(slices[0].mem_, "resp", 4);
        return makeSuccessResult(4);
      }));
  EXPECT_EQ(4, BIO_read(bio_, rbuf, 16));
  EXPECT_EQ(std::string(rbuf, 4), "resp");
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, ReadRetryThenEof) {
  char buf[16];
  // EAGAIN followed by EOF (0 bytes).
  EXPECT_CALL(io_handle_, readv(10, _, 1))
      .WillOnce(Return(testing::ByMove(makeAgainResult())))
      .WillOnce(Return(testing::ByMove(makeSuccessResult(0))));

  EXPECT_EQ(-1, BIO_read(bio_, buf, 10));
  EXPECT_TRUE(BIO_should_retry(bio_));

  EXPECT_EQ(0, BIO_read(bio_, buf, 10));
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, WriteRetryThenSuccess) {
  const char data[] = "hello";
  // EAGAIN, then interrupt, then success.
  EXPECT_CALL(io_handle_, writev(_, 1))
      .WillOnce(Return(testing::ByMove(makeAgainResult())))
      .WillOnce(Return(testing::ByMove(makeErrorResult(SOCKET_ERROR_INTR))))
      .WillOnce(Return(testing::ByMove(makeSuccessResult(5))));

  EXPECT_EQ(-1, BIO_write(bio_, data, 5));
  EXPECT_TRUE(BIO_should_retry(bio_));

  EXPECT_EQ(-1, BIO_write(bio_, data, 5));
  EXPECT_TRUE(BIO_should_retry(bio_));

  EXPECT_EQ(5, BIO_write(bio_, data, 5));
  EXPECT_FALSE(BIO_should_retry(bio_));
}

// Test that two independent BIOs backed by different io_handles don't interfere.
TEST(IoHandleBioIndependentTest, TwoBiosAreIndependent) {
  NiceMock<Network::MockIoHandle> handle1;
  NiceMock<Network::MockIoHandle> handle2;
  BIO* bio1 = BIO_new_io_handle(&handle1);
  BIO* bio2 = BIO_new_io_handle(&handle2);
  ASSERT_NE(nullptr, bio1);
  ASSERT_NE(nullptr, bio2);

  // Verify each BIO points to its own io_handle.
  EXPECT_EQ(&handle1, BIO_get_data(bio1));
  EXPECT_EQ(&handle2, BIO_get_data(bio2));

  char buf[16];

  // Read from bio1 should call handle1.
  EXPECT_CALL(handle1, readv(10, _, 1)).WillOnce(Return(testing::ByMove(makeSuccessResult(3))));
  EXPECT_CALL(handle2, readv(_, _, _)).Times(0);
  EXPECT_EQ(3, BIO_read(bio1, buf, 10));

  // Write to bio2 should call handle2.
  EXPECT_CALL(handle2, writev(_, 1)).WillOnce(Return(testing::ByMove(makeSuccessResult(5))));
  EXPECT_CALL(handle1, writev(_, _)).Times(0);
  EXPECT_EQ(5, BIO_write(bio2, "hello", 5));

  // Retry state on one BIO doesn't affect the other.
  EXPECT_CALL(handle1, readv(10, _, 1)).WillOnce(Return(testing::ByMove(makeAgainResult())));
  EXPECT_EQ(-1, BIO_read(bio1, buf, 10));
  EXPECT_TRUE(BIO_should_retry(bio1));
  EXPECT_FALSE(BIO_should_retry(bio2));

  BIO_free(bio1);
  BIO_free(bio2);
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
