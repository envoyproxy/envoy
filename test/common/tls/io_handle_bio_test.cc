#include <limits>

#include "source/common/network/io_socket_error_impl.h"
#include "source/common/tls/io_handle_bio.h"

#include "test/common/memory/memory_test_utility.h"
#include "test/mocks/network/io_handle.h"
#include "test/test_common/test_runtime.h"

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

  EXPECT_NE(nullptr, BIO_get_data(bio1));
  EXPECT_NE(nullptr, BIO_get_data(bio2));
  EXPECT_NE(BIO_get_data(bio1), BIO_get_data(bio2));

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

TEST_F(IoHandleBioTest, ZeroReadAheadSizePreservesDirectReads) {
  EXPECT_TRUE(enableIoHandleBioReadAhead(bio_, 0));
  char out[17];
  EXPECT_CALL(io_handle_, readv(sizeof(out), _, 1))
      .WillOnce(Invoke([&out](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        EXPECT_EQ(out, slices[0].mem_);
        EXPECT_EQ(sizeof(out), slices[0].len_);
        memcpy(slices[0].mem_, "abc", 3);
        return makeSuccessResult(3);
      }));
  EXPECT_EQ(3, BIO_read(bio_, out, sizeof(out)));
  EXPECT_EQ("abc", std::string(out, 3));
  EXPECT_EQ(0, BIO_pending(bio_));
}

TEST_F(IoHandleBioTest, ReadAheadRejectsOtherBioTypes) {
  EXPECT_FALSE(enableIoHandleBioReadAhead(nullptr, 16384));
  const char input[] = "foreign BIO";
  bssl::UniquePtr<BIO> memory_bio(BIO_new_mem_buf(input, sizeof(input)));
  ASSERT_NE(nullptr, memory_bio);
  EXPECT_FALSE(enableIoHandleBioReadAhead(memory_bio.get(), 16384));
  EXPECT_EQ(sizeof(input), BIO_pending(memory_bio.get()));
  char out[sizeof(input)];
  EXPECT_EQ(sizeof(input), BIO_read(memory_bio.get(), out, sizeof(out)));
  EXPECT_EQ(std::string(input, sizeof(input)), std::string(out, sizeof(out)));

  bssl::UniquePtr<BIO> socket_bio(BIO_new(BIO_s_socket()));
  ASSERT_NE(nullptr, socket_bio);
  EXPECT_NE(BIO_method_type(socket_bio.get()), BIO_method_type(bio_));
  EXPECT_FALSE(enableIoHandleBioReadAhead(socket_bio.get(), 16384));
}

class IoHandleBioReadAheadSizeTest : public IoHandleBioTest,
                                     public testing::WithParamInterface<uint32_t> {};

INSTANTIATE_TEST_SUITE_P(Sizes, IoHandleBioReadAheadSizeTest,
                         testing::Values(1, 7, 16384, 65536, 128 * 1024, 1024 * 1024));

TEST_P(IoHandleBioReadAheadSizeTest, LargerAndExactRequestsAreBoundedAndReuseBuffer) {
  const uint32_t size = GetParam();
  EXPECT_TRUE(enableIoHandleBioReadAhead(bio_, size));
  std::string out(size + 17, '\0');
  void* buffer = nullptr;
  EXPECT_CALL(io_handle_, readv(size, _, 1))
      .Times(2)
      .WillRepeatedly(Invoke([&out, &buffer, size](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        EXPECT_EQ(size, slices[0].len_);
        EXPECT_NE(out.data(), slices[0].mem_);
        if (buffer == nullptr) {
          buffer = slices[0].mem_;
        } else {
          EXPECT_EQ(buffer, slices[0].mem_);
        }
        memset(slices[0].mem_, 'x', size);
        return makeSuccessResult(size);
      }));
  EXPECT_EQ(size, BIO_read(bio_, out.data(), out.size()));
  EXPECT_EQ(std::string(size, 'x'), out.substr(0, size));
  EXPECT_EQ(std::string(17, '\0'), out.substr(size));
  EXPECT_EQ(0, BIO_pending(bio_));
  EXPECT_EQ(size, BIO_read(bio_, out.data(), size));
  EXPECT_EQ(std::string(size, 'x'), out.substr(0, size));
  EXPECT_EQ(0, BIO_pending(bio_));
}

TEST_P(IoHandleBioReadAheadSizeTest, PendingBytesAreReturnedBeforeRefilling) {
  const uint32_t size = GetParam();
  enableIoHandleBioReadAhead(bio_, size);
  std::string out(size + 1, '\0');
  EXPECT_CALL(io_handle_, readv(size, _, 1))
      .WillOnce(Invoke([size](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memset(slices[0].mem_, 'x', size);
        return makeSuccessResult(size);
      }))
      .WillOnce(Return(testing::ByMove(makeAgainResult())));
  EXPECT_EQ(1, BIO_read(bio_, out.data(), 1));
  EXPECT_EQ('x', out[0]);
  EXPECT_EQ(size - 1, BIO_pending(bio_));
  if (size > 1) {
    EXPECT_EQ(size - 1, BIO_read(bio_, out.data(), out.size()));
    EXPECT_EQ(std::string(size - 1, 'x'), out.substr(0, size - 1));
  }
  EXPECT_EQ(0, BIO_pending(bio_));
  EXPECT_EQ(-1, BIO_read(bio_, out.data(), out.size()));
  EXPECT_TRUE(BIO_should_read(bio_));
}

class IoHandleBioReadAheadAllocationTest : public IoHandleBioTest,
                                           public testing::WithParamInterface<uint32_t> {};

INSTANTIATE_TEST_SUITE_P(Sizes, IoHandleBioReadAheadAllocationTest,
                         testing::Values(0, 16384, 1024 * 1024,
                                         std::numeric_limits<uint32_t>::max()));

TEST_P(IoHandleBioReadAheadAllocationTest, ConfigurationAndInvalidReadsDoNotAllocate) {
  EXPECT_CALL(io_handle_, readv(_, _, _)).Times(0);
  char out;
  Memory::TestUtil::MemoryTest memory_test;
  enableIoHandleBioReadAhead(bio_, GetParam());
  const size_t configured_bytes = memory_test.consumedBytes();
  const int null_read = BIO_read(bio_, nullptr, 1);
  const int empty_read = BIO_read(bio_, &out, 0);
  const size_t after_invalid_reads = memory_test.consumedBytes();
  EXPECT_MEMORY_EQ(configured_bytes, 0);
  EXPECT_MEMORY_EQ(after_invalid_reads, 0);
  EXPECT_EQ(0, null_read);
  EXPECT_EQ(0, empty_read);
  EXPECT_EQ(0, BIO_pending(bio_));
}

TEST_F(IoHandleBioTest, ReadAheadDoesNotChangeWrites) {
  enableIoHandleBioReadAhead(bio_, std::numeric_limits<uint32_t>::max());
  const char out[] = "write without allocating a read buffer";
  EXPECT_CALL(io_handle_, readv(_, _, _)).Times(0);
  EXPECT_CALL(io_handle_, writev(_, 1))
      .WillOnce(Invoke([&out](const Buffer::RawSlice* slices, uint64_t) {
        EXPECT_EQ(out, slices[0].mem_);
        EXPECT_EQ(sizeof(out), slices[0].len_);
        return makeSuccessResult(sizeof(out));
      }));
  EXPECT_EQ(sizeof(out), BIO_write(bio_, out, sizeof(out)));
  EXPECT_FALSE(BIO_should_retry(bio_));
  EXPECT_EQ(0, BIO_pending(bio_));
}

TEST_F(IoHandleBioTest, ReadAheadDrainsBeforeReadingAgain) {
  enableIoHandleBioReadAhead(bio_, 16384);
  char out[20];
  EXPECT_EQ(0, BIO_pending(bio_));
  EXPECT_CALL(io_handle_, readv(16384, _, 1))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        EXPECT_EQ(16384, slices[0].len_);
        memcpy(slices[0].mem_, "helloworld!", 11);
        return makeSuccessResult(11);
      }));
  EXPECT_EQ(5, BIO_read(bio_, out, 5));
  EXPECT_EQ("hello", std::string(out, 5));
  EXPECT_EQ(6, BIO_pending(bio_));
  // Re-enabling preserves buffered ciphertext.
  enableIoHandleBioReadAhead(bio_, 16384);
  EXPECT_EQ(1, BIO_read(bio_, out, 1));
  EXPECT_EQ('w', out[0]);
  EXPECT_EQ(5, BIO_pending(bio_));
  EXPECT_EQ(5, BIO_read(bio_, out, sizeof(out)));
  EXPECT_EQ("orld!", std::string(out, 5));
  EXPECT_EQ(0, BIO_pending(bio_));
}

TEST_F(IoHandleBioTest, ReadAheadPartialInput) {
  enableIoHandleBioReadAhead(bio_, 7);
  char out[10];
  EXPECT_CALL(io_handle_, readv(7, _, 1))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memcpy(slices[0].mem_, "abc", 3);
        return makeSuccessResult(3);
      }))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memcpy(slices[0].mem_, "defgh", 5);
        return makeSuccessResult(5);
      }));
  EXPECT_EQ(3, BIO_read(bio_, out, sizeof(out)));
  EXPECT_EQ("abc", std::string(out, 3));
  EXPECT_EQ(2, BIO_read(bio_, out, 2));
  EXPECT_EQ("de", std::string(out, 2));
  EXPECT_EQ(3, BIO_read(bio_, out, sizeof(out)));
  EXPECT_EQ("fgh", std::string(out, 3));
}

class IoHandleBioEofTest : public IoHandleBioTest, public testing::WithParamInterface<uint32_t> {};

INSTANTIATE_TEST_SUITE_P(Sizes, IoHandleBioEofTest, testing::Values(0, 16384));

TEST_P(IoHandleBioEofTest, EofReportsPendingSocketError) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ssl_socket_report_connection_reset", "true"}});
  enableIoHandleBioReadAhead(bio_, GetParam());
  char out[10];
  EXPECT_CALL(io_handle_, readv(GetParam() == 0 ? sizeof(out) : GetParam(), _, 1))
      .WillOnce(Return(testing::ByMove(makeSuccessResult(0))));
  EXPECT_CALL(io_handle_, getOption(SOL_SOCKET, SO_ERROR, _, _))
      .WillOnce(Invoke([](int, int, void* value, socklen_t* len) {
        EXPECT_EQ(sizeof(int), *len);
        *static_cast<int*>(value) = ECONNRESET;
        return Api::SysCallIntResult{0, 0};
      }));
  EXPECT_EQ(0, BIO_read(bio_, out, sizeof(out)));
  const auto error = ERR_get_error();
  EXPECT_EQ(ERR_LIB_SYS, ERR_GET_LIB(error));
  EXPECT_EQ(ECONNRESET, ERR_GET_REASON(error));
  EXPECT_EQ(0, ERR_peek_error());
  EXPECT_FALSE(BIO_should_retry(bio_));
  EXPECT_EQ(0, BIO_pending(bio_));
}

TEST_P(IoHandleBioEofTest, EofWithoutPendingSocketError) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ssl_socket_report_connection_reset", "true"}});
  enableIoHandleBioReadAhead(bio_, GetParam());
  char out[10];
  EXPECT_CALL(io_handle_, readv(GetParam() == 0 ? sizeof(out) : GetParam(), _, 1))
      .WillOnce(Return(testing::ByMove(makeSuccessResult(0))));
  EXPECT_CALL(io_handle_, getOption(SOL_SOCKET, SO_ERROR, _, _))
      .WillOnce(Invoke([](int, int, void* value, socklen_t*) {
        *static_cast<int*>(value) = 0;
        return Api::SysCallIntResult{0, 0};
      }));
  EXPECT_EQ(0, BIO_read(bio_, out, sizeof(out)));
  EXPECT_EQ(0, ERR_peek_error());
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_P(IoHandleBioEofTest, EofIgnoresFailedSocketErrorLookup) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ssl_socket_report_connection_reset", "true"}});
  enableIoHandleBioReadAhead(bio_, GetParam());
  char out[10];
  EXPECT_CALL(io_handle_, readv(GetParam() == 0 ? sizeof(out) : GetParam(), _, 1))
      .WillOnce(Return(testing::ByMove(makeSuccessResult(0))));
  EXPECT_CALL(io_handle_, getOption(SOL_SOCKET, SO_ERROR, _, _))
      .WillOnce(Invoke([](int, int, void* value, socklen_t*) {
        *static_cast<int*>(value) = ECONNRESET;
        return Api::SysCallIntResult{-1, EBADF};
      }));
  EXPECT_EQ(0, BIO_read(bio_, out, sizeof(out)));
  EXPECT_EQ(0, ERR_peek_error());
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_P(IoHandleBioEofTest, EofSocketErrorLookupDisabledByRuntime) {
  TestScopedRuntime scoped_runtime;
  scoped_runtime.mergeValues(
      {{"envoy.reloadable_features.ssl_socket_report_connection_reset", "false"}});
  enableIoHandleBioReadAhead(bio_, GetParam());
  char out[10];
  EXPECT_CALL(io_handle_, readv(GetParam() == 0 ? sizeof(out) : GetParam(), _, 1))
      .WillOnce(Return(testing::ByMove(makeSuccessResult(0))));
  EXPECT_CALL(io_handle_, getOption(_, _, _, _)).Times(0);
  EXPECT_EQ(0, BIO_read(bio_, out, sizeof(out)));
  EXPECT_EQ(0, ERR_peek_error());
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, ReadAheadRetryFlagsClearOnCachedRead) {
  enableIoHandleBioReadAhead(bio_, 16384);
  char out[4];
  EXPECT_CALL(io_handle_, readv(16384, _, 1))
      .WillOnce(Return(testing::ByMove(makeAgainResult())))
      .WillOnce(Return(testing::ByMove(makeErrorResult(SOCKET_ERROR_INTR))))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memcpy(slices[0].mem_, "abcd", 4);
        return makeSuccessResult(4);
      }));
  EXPECT_EQ(-1, BIO_read(bio_, out, 1));
  EXPECT_TRUE(BIO_should_read(bio_));
  EXPECT_EQ(-1, BIO_read(bio_, out, 1));
  EXPECT_TRUE(BIO_should_read(bio_));
  EXPECT_EQ(0, ERR_peek_error());
  EXPECT_EQ(1, BIO_read(bio_, out, 1));
  EXPECT_FALSE(BIO_should_retry(bio_));
  EXPECT_CALL(io_handle_, writev(_, 1)).WillOnce(Return(testing::ByMove(makeAgainResult())));
  EXPECT_EQ(-1, BIO_write(bio_, "x", 1));
  EXPECT_TRUE(BIO_should_write(bio_));
  EXPECT_EQ(3, BIO_read(bio_, out, sizeof(out)));
  EXPECT_EQ("bcd", std::string(out, 3));
  EXPECT_FALSE(BIO_should_retry(bio_));
}

TEST_F(IoHandleBioTest, ReadAheadDrainsBeforeEof) {
  enableIoHandleBioReadAhead(bio_, 16384);
  char out[4];
  EXPECT_CALL(io_handle_, readv(16384, _, 1))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memcpy(slices[0].mem_, "abcd", 4);
        return makeSuccessResult(4);
      }))
      .WillOnce(Return(testing::ByMove(makeSuccessResult(0))));
  EXPECT_EQ(1, BIO_read(bio_, out, 1));
  EXPECT_EQ(3, BIO_read(bio_, out, sizeof(out)));
  EXPECT_EQ("bcd", std::string(out, 3));
  EXPECT_EQ(0, BIO_read(bio_, out, sizeof(out)));
  EXPECT_FALSE(BIO_should_retry(bio_));
  EXPECT_EQ(0, BIO_pending(bio_));
}

TEST_F(IoHandleBioTest, ReadAheadDrainsBeforeError) {
  enableIoHandleBioReadAhead(bio_, 16384);
  char out[4];
  EXPECT_CALL(io_handle_, readv(16384, _, 1))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memcpy(slices[0].mem_, "abcd", 4);
        return makeSuccessResult(4);
      }))
      .WillOnce(Return(testing::ByMove(makeErrorResult(ECONNRESET))));
  EXPECT_EQ(1, BIO_read(bio_, out, 1));
  EXPECT_EQ(3, BIO_read(bio_, out, sizeof(out)));
  EXPECT_EQ("bcd", std::string(out, 3));
  EXPECT_EQ(-1, BIO_read(bio_, out, sizeof(out)));
  EXPECT_FALSE(BIO_should_retry(bio_));
  EXPECT_EQ(ECONNRESET, ERR_GET_REASON(ERR_get_error()));
  EXPECT_EQ(0, BIO_pending(bio_));
}

TEST_F(IoHandleBioTest, ReadAheadInvalidReadDoesNotConsumeInput) {
  enableIoHandleBioReadAhead(bio_, 16384);
  char out;
  EXPECT_CALL(io_handle_, readv(_, _, _)).Times(0);
  EXPECT_EQ(0, BIO_read(bio_, nullptr, 1));
  EXPECT_EQ(0, BIO_read(bio_, &out, 0));
  EXPECT_LE(BIO_read(bio_, &out, -1), 0);
  EXPECT_EQ(0, BIO_pending(bio_));
  ERR_clear_error();
}

TEST(IoHandleBioIndependentTest, ReadAheadStateAndLifetimeAreIndependent) {
  testing::StrictMock<Network::MockIoHandle> first;
  testing::StrictMock<Network::MockIoHandle> second;
  bssl::UniquePtr<BIO> first_bio(BIO_new_io_handle(&first));
  bssl::UniquePtr<BIO> second_bio(BIO_new_io_handle(&second));
  enableIoHandleBioReadAhead(first_bio.get(), 16384);
  enableIoHandleBioReadAhead(second_bio.get(), 7);
  EXPECT_CALL(first, readv(16384, _, 1))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memcpy(slices[0].mem_, "abc", 3);
        return makeSuccessResult(3);
      }));
  EXPECT_CALL(second, readv(7, _, 1))
      .WillOnce(Invoke([](uint64_t, Buffer::RawSlice* slices, uint64_t) {
        memcpy(slices[0].mem_, "xyz", 3);
        return makeSuccessResult(3);
      }));
  char out[4];
  EXPECT_EQ(1, BIO_read(first_bio.get(), out, 1));
  EXPECT_EQ('a', out[0]);
  EXPECT_EQ(1, BIO_read(second_bio.get(), out, 1));
  EXPECT_EQ('x', out[0]);
  first_bio.reset(); // Free queued ciphertext without closing the borrowed IoHandle.
  EXPECT_EQ(2, BIO_pending(second_bio.get()));
  EXPECT_EQ(2, BIO_read(second_bio.get(), out, sizeof(out)));
  EXPECT_EQ("yz", std::string(out, 2));
}

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
