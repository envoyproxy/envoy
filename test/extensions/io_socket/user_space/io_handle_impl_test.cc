#include "envoy/buffer/buffer.h"
#include "envoy/event/file_event.h"

#include "source/common/buffer/buffer_impl.h"
#include "source/common/network/address_impl.h"
#include "source/common/stream_info/filter_state_impl.h"
#include "source/extensions/io_socket/user_space/io_handle_impl.h"

#include "test/mocks/event/mocks.h"

#include "absl/container/fixed_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {
namespace {

constexpr int CONNECTED = 0;

MATCHER(IsInvalidAddress, "") {
  return arg.err_->getErrorCode() == Api::IoError::IoErrorCode::NoSupport;
}

MATCHER(IsNotSupportedResult, "") { return arg.errno_ == SOCKET_ERROR_NOT_SUP; }

ABSL_MUST_USE_RESULT std::pair<Buffer::Slice, Buffer::RawSlice> allocateOneSlice(uint64_t size) {
  Buffer::Slice mutable_slice(size, nullptr);
  auto slice = mutable_slice.reserve(size);
  EXPECT_NE(nullptr, slice.mem_);
  EXPECT_EQ(size, slice.len_);
  return {std::move(mutable_slice), slice};
}

class MockFileEventCallback {
public:
  MOCK_METHOD(void, called, (uint32_t arg));
};

class IoHandleImplTest : public testing::Test {
public:
  IoHandleImplTest() : buf_(1024) {
    std::tie(io_handle_, io_handle_peer_) = IoHandleFactory::createIoHandlePair();
  }

  NiceMock<Event::MockDispatcher> dispatcher_;

  // Owned by IoHandleImpl.
  NiceMock<Event::MockSchedulableCallback>* schedulable_cb_;
  MockFileEventCallback cb_;
  std::unique_ptr<IoHandleImpl> io_handle_;
  std::unique_ptr<IoHandleImpl> io_handle_peer_;
  absl::FixedArray<char> buf_;
};

TEST_F(IoHandleImplTest, InterfaceName) { ASSERT_FALSE(io_handle_->interfaceName().has_value()); }

// Test recv side effects.
TEST_F(IoHandleImplTest, BasicRecv) {
  Buffer::OwnedImpl buf_to_write("0123456789");
  io_handle_peer_->write(buf_to_write);
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    ASSERT_EQ(10, result.return_value_);
    ASSERT_EQ("0123456789", absl::string_view(buf_.data(), result.return_value_));
  }
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    // `EAGAIN`.
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  }
  {
    io_handle_->setWriteEnd();
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    EXPECT_TRUE(result.ok());
  }
}

// Test recv side effects.
TEST_F(IoHandleImplTest, RecvPeek) {
  Buffer::OwnedImpl buf_to_write("0123456789");
  io_handle_peer_->write(buf_to_write);
  {
    ::memset(buf_.data(), 1, buf_.size());
    auto result = io_handle_->recv(buf_.data(), 5, MSG_PEEK);
    ASSERT_EQ(5, result.return_value_);
    ASSERT_EQ("01234", absl::string_view(buf_.data(), result.return_value_));
    // The data beyond the boundary is untouched.
    ASSERT_EQ(std::string(buf_.size() - 5, 1), absl::string_view(buf_.data() + 5, buf_.size() - 5));
  }
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
    ASSERT_EQ(10, result.return_value_);
    ASSERT_EQ("0123456789", absl::string_view(buf_.data(), result.return_value_));
  }
  {
    // Drain the pending buffer.
    auto recv_result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    EXPECT_TRUE(recv_result.ok());
    EXPECT_EQ(10, recv_result.return_value_);
    ASSERT_EQ("0123456789", absl::string_view(buf_.data(), recv_result.return_value_));
    auto peek_result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    // `EAGAIN`.
    EXPECT_FALSE(peek_result.ok());
    EXPECT_EQ(Api::IoError::IoErrorCode::Again, peek_result.err_->getErrorCode());
  }
  {
    // Peek upon shutdown.
    io_handle_->setWriteEnd();
    auto result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
    EXPECT_EQ(0, result.return_value_);
    ASSERT(result.ok());
  }
}

TEST_F(IoHandleImplTest, RecvPeekWhenPendingDataButShutdown) {
  Buffer::OwnedImpl buf_to_write("0123456789");
  io_handle_peer_->write(buf_to_write);
  auto result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
  ASSERT_EQ(10, result.return_value_);
  ASSERT_EQ("0123456789", absl::string_view(buf_.data(), result.return_value_));
}

TEST_F(IoHandleImplTest, MultipleRecvDrain) {
  Buffer::OwnedImpl buf_to_write("abcd");
  io_handle_peer_->write(buf_to_write);
  {
    auto result = io_handle_->recv(buf_.data(), 1, 0);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(1, result.return_value_);
    EXPECT_EQ("a", absl::string_view(buf_.data(), 1));
  }
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(3, result.return_value_);

    EXPECT_EQ("bcd", absl::string_view(buf_.data(), 3));
    EXPECT_EQ(0, io_handle_->getWriteBuffer()->length());
  }
}

// Test read side effects.
TEST_F(IoHandleImplTest, ReadEmpty) {
  Buffer::OwnedImpl buf;
  auto result = io_handle_->read(buf, 10);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  io_handle_->setWriteEnd();
  result = io_handle_->read(buf, 10);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(0, result.return_value_);
}

// Read allows max_length value 0 and returns no error.
TEST_F(IoHandleImplTest, ReadWhileProvidingNoCapacity) {
  Buffer::OwnedImpl buf;
  absl::optional<uint64_t> max_length_opt{0};
  auto result = io_handle_->read(buf, max_length_opt);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(0, result.return_value_);
}

// Test read side effects.
TEST_F(IoHandleImplTest, ReadContent) {
  Buffer::OwnedImpl buf_to_write("abcdefg");
  io_handle_peer_->write(buf_to_write);

  Buffer::OwnedImpl buf;
  auto result = io_handle_->read(buf, 3);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(3, result.return_value_);
  ASSERT_EQ(3, buf.length());
  ASSERT_EQ(4, io_handle_->getWriteBuffer()->length());
  result = io_handle_->read(buf, 10);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(4, result.return_value_);
  ASSERT_EQ(7, buf.length());
  ASSERT_EQ(0, io_handle_->getWriteBuffer()->length());
}

TEST_F(IoHandleImplTest, WriteClearsDrainTrackers) {
  Buffer::OwnedImpl buf_to_write("abcdefg");
  {
    bool called = false;
    // This drain tracker should be called as soon as the write happens; not on read.
    buf_to_write.addDrainTracker([&called]() { called = true; });
    io_handle_peer_->write(buf_to_write);
    EXPECT_TRUE(called);
  }
  // Now the drain tracker refers to a stack variable that no longer exists. If the drain tracker
  // is called subsequently, this will fail in ASan.
  Buffer::OwnedImpl buf;
  auto result = io_handle_->read(buf, 10);
  ASSERT_TRUE(result.ok());
  ASSERT_EQ(7, result.return_value_);
}

// Test read throttling on watermark buffer.
TEST_F(IoHandleImplTest, ReadThrottling) {
  {
    // Prepare data to read.
    Buffer::OwnedImpl buf_to_write(std::string(13 * FRAGMENT_SIZE, 'a'));
    while (buf_to_write.length() > 0) {
      io_handle_peer_->write(buf_to_write);
    }
  }
  Buffer::OwnedImpl unlimited_buf;
  {
    // Read at most 8 * FRAGMENT_SIZE to unlimited buffer.
    auto result0 = io_handle_->read(unlimited_buf, absl::nullopt);
    EXPECT_TRUE(result0.ok());
    EXPECT_EQ(result0.return_value_, 8 * FRAGMENT_SIZE);
    EXPECT_EQ(unlimited_buf.length(), 8 * FRAGMENT_SIZE);
    EXPECT_EQ(unlimited_buf.toString(), std::string(8 * FRAGMENT_SIZE, 'a'));
  }

  Buffer::WatermarkBuffer buf([]() {}, []() {}, []() {});
  buf.setWatermarks(FRAGMENT_SIZE + 1);
  {
    // Verify that read() populates the buf to high watermark.
    auto result = io_handle_->read(buf, 8 * FRAGMENT_SIZE + 1);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.return_value_, FRAGMENT_SIZE + 1);
    EXPECT_EQ(buf.length(), FRAGMENT_SIZE + 1);
    EXPECT_FALSE(buf.highWatermarkTriggered());
    EXPECT_EQ(buf.toString(), std::string(FRAGMENT_SIZE + 1, 'a'));
  }

  {
    // Verify that read returns FRAGMENT_SIZE if the buf is over high watermark.
    auto result1 = io_handle_->read(buf, 8 * FRAGMENT_SIZE + 1);
    EXPECT_TRUE(result1.ok());
    EXPECT_EQ(result1.return_value_, FRAGMENT_SIZE);
    EXPECT_EQ(buf.length(), 2 * FRAGMENT_SIZE + 1);
    EXPECT_TRUE(buf.highWatermarkTriggered());
    EXPECT_EQ(buf.toString(), std::string(2 * FRAGMENT_SIZE + 1, 'a'));
  }
  {
    // Verify that read() returns FRAGMENT_SIZE bytes if the prepared buf is 1 byte away from high
    // watermark. The buf highWatermarkTriggered is true before the read.
    buf.drain(FRAGMENT_SIZE + 1);
    EXPECT_EQ(buf.length(), buf.highWatermark() - 1);
    EXPECT_TRUE(buf.highWatermarkTriggered());
    auto result2 = io_handle_->read(buf, 8 * FRAGMENT_SIZE + 1);
    EXPECT_TRUE(result2.ok());
    EXPECT_EQ(result2.return_value_, FRAGMENT_SIZE);
    EXPECT_TRUE(buf.highWatermarkTriggered());
    EXPECT_EQ(buf.toString(), std::string(buf.highWatermark() - 1 + FRAGMENT_SIZE, 'a'));
  }
  {
    // Verify that read() returns FRAGMENT_SIZE bytes if the prepared buf is 1 byte away from high
    // watermark. The buf highWatermarkTriggered is false before the read.
    buf.drain(buf.length());
    buf.add(std::string(buf.highWatermark() - 1, 'a'));
    EXPECT_EQ(buf.length(), buf.highWatermark() - 1);
    // Buffer is populated from below low watermark to high watermark - 1, so the high watermark is
    // not triggered yet.
    EXPECT_FALSE(buf.highWatermarkTriggered());
    auto result3 = io_handle_->read(buf, 8 * FRAGMENT_SIZE + 1);
    EXPECT_TRUE(result3.ok());
    EXPECT_EQ(result3.return_value_, FRAGMENT_SIZE);
    EXPECT_TRUE(buf.highWatermarkTriggered());
    EXPECT_EQ(buf.toString(), std::string(buf.highWatermark() - 1 + FRAGMENT_SIZE, 'a'));
  }
}

// Test readv behavior.
TEST_F(IoHandleImplTest, BasicReadv) {
  Buffer::OwnedImpl buf_to_write("abc");
  io_handle_peer_->write(buf_to_write);

  Buffer::OwnedImpl buf;
  auto reservation = buf.reserveSingleSlice(1024);
  auto slice = reservation.slice();
  auto result = io_handle_->readv(1024, &slice, 1);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(3, result.return_value_);

  result = io_handle_->readv(1024, &slice, 1);

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());

  io_handle_->setWriteEnd();
  result = io_handle_->readv(1024, &slice, 1);
  // EOF
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(0, result.return_value_);
}

// Test readv on slices.
TEST_F(IoHandleImplTest, ReadvMultiSlices) {
  Buffer::OwnedImpl buf_to_write(std::string(1024, 'a'));
  io_handle_peer_->write(buf_to_write);

  char full_frag[1024];
  Buffer::RawSlice slices[2] = {{full_frag, 512}, {full_frag + 512, 512}};

  auto result = io_handle_->readv(1024, slices, 2);

  EXPECT_EQ(absl::string_view(full_frag, 1024), std::string(1024, 'a'));
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(1024, result.return_value_);
}

TEST_F(IoHandleImplTest, FlowControl) {
  io_handle_->setWatermarks(128);
  EXPECT_FALSE(io_handle_->isReadable());
  EXPECT_TRUE(io_handle_->isWritable());

  // Populate the data for io_handle_.
  Buffer::OwnedImpl buffer(std::string(256, 'a'));
  io_handle_peer_->write(buffer);

  EXPECT_TRUE(io_handle_->isReadable());
  EXPECT_FALSE(io_handle_->isWritable());

  bool writable_flipped = false;
  // During the repeated recv, the writable flag must switch to true.
  auto& internal_buffer = *io_handle_->getWriteBuffer();
  while (internal_buffer.length() > 0) {
    SCOPED_TRACE(internal_buffer.length());
    ENVOY_LOG_MISC(debug, "internal buffer length = {}", internal_buffer.length());
    EXPECT_TRUE(io_handle_->isReadable());
    bool writable = io_handle_->isWritable();
    ENVOY_LOG_MISC(debug, "internal buffer length = {}, writable = {}", internal_buffer.length(),
                   writable);
    if (writable) {
      writable_flipped = true;
    } else {
      ASSERT_FALSE(writable_flipped);
    }
    auto result = io_handle_->recv(buf_.data(), 32, 0);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(32, result.return_value_);
  }
  ASSERT_EQ(0, internal_buffer.length());
  ASSERT_TRUE(writable_flipped);

  // Finally the buffer is empty.
  EXPECT_FALSE(io_handle_->isReadable());
  EXPECT_TRUE(io_handle_->isWritable());
}

// Consistent with other IoHandle: allow write empty data when handle is closed.
TEST_F(IoHandleImplTest, NoErrorWriteZeroDataToClosedIoHandle) {
  io_handle_->close();
  {
    Buffer::OwnedImpl buf;
    auto result = io_handle_->write(buf);
    ASSERT_EQ(0, result.return_value_);
    ASSERT(result.ok());
  }
  {
    Buffer::RawSlice slice{nullptr, 0};
    auto result = io_handle_->writev(&slice, 1);
    ASSERT_EQ(0, result.return_value_);
    ASSERT(result.ok());
  }
}

TEST_F(IoHandleImplTest, ErrorOnClosedIoHandle) {
  io_handle_->close();
  {
    auto [guard, slice] = allocateOneSlice(1024);
    auto result = io_handle_->recv(slice.mem_, slice.len_, 0);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::BadFd, result.err_->getErrorCode());
  }
  {
    Buffer::OwnedImpl buf;
    auto result = io_handle_->read(buf, 10);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::BadFd, result.err_->getErrorCode());
  }
  {
    auto [guard, slice] = allocateOneSlice(1024);
    auto result = io_handle_->readv(1024, &slice, 1);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::BadFd, result.err_->getErrorCode());
  }
  {
    Buffer::OwnedImpl buf("0123456789");
    auto result = io_handle_->write(buf);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::BadFd, result.err_->getErrorCode());
  }
  {
    Buffer::OwnedImpl buf("0123456789");
    auto slices = buf.getRawSlices();
    ASSERT(!slices.empty());
    auto result = io_handle_->writev(slices.data(), slices.size());
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::BadFd, result.err_->getErrorCode());
  }
}

TEST_F(IoHandleImplTest, RepeatedShutdownWR) {
  EXPECT_EQ(io_handle_peer_->shutdown(ENVOY_SHUT_WR).return_value_, 0);
  EXPECT_EQ(io_handle_peer_->shutdown(ENVOY_SHUT_WR).return_value_, 0);
}

TEST_F(IoHandleImplTest, ShutDownOptionsNotSupported) {
  ASSERT_DEBUG_DEATH(io_handle_peer_->shutdown(ENVOY_SHUT_RD), "");
  ASSERT_DEBUG_DEATH(io_handle_peer_->shutdown(ENVOY_SHUT_RDWR), "");
}

// This test is ensure the memory created by BufferFragment won't be released
// after the write.
TEST_F(IoHandleImplTest, WriteBufferFragement) {
  Buffer::OwnedImpl buf("a");
  bool released = false;
  auto buf_frag = Buffer::OwnedBufferFragmentImpl::create(
      std::string(255, 'b'), [&released](const Buffer::OwnedBufferFragmentImpl* fragment) {
        released = true;
        delete fragment;
      });
  buf.addBufferFragment(*buf_frag.release());

  auto result = io_handle_->write(buf);
  EXPECT_FALSE(released);
  EXPECT_EQ(0, buf.length());
  io_handle_peer_->read(buf, absl::nullopt);
  buf.drain(buf.length());
  EXPECT_TRUE(released);
}

TEST_F(IoHandleImplTest, WriteByMove) {
  Buffer::OwnedImpl buf("0123456789");
  auto result = io_handle_peer_->write(buf);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(10, result.return_value_);
  EXPECT_EQ("0123456789", io_handle_->getWriteBuffer()->toString());
  EXPECT_EQ(0, buf.length());
}

// Test write return error code. Ignoring the side effect of event scheduling.
TEST_F(IoHandleImplTest, WriteAgain) {
  // Populate write destination with massive data so as to not writable.
  io_handle_peer_->setWatermarks(128);
  Buffer::OwnedImpl pending_data(std::string(256, 'a'));
  io_handle_->write(pending_data);
  EXPECT_FALSE(io_handle_peer_->isWritable());

  Buffer::OwnedImpl buf("0123456789");
  auto result = io_handle_->write(buf);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
  EXPECT_EQ(10, buf.length());
}

TEST_F(IoHandleImplTest, PartialWrite) {
  const uint64_t INITIAL_SIZE = 4 * FRAGMENT_SIZE;
  io_handle_peer_->setWatermarks(FRAGMENT_SIZE + 1);

  Buffer::OwnedImpl pending_data(std::string(INITIAL_SIZE, 'a'));

  {
    // Write until high watermark. The write bytes reaches high watermark value but high watermark
    // is not triggered.
    auto result = io_handle_->write(pending_data);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(result.return_value_, FRAGMENT_SIZE + 1);
    EXPECT_EQ(pending_data.length(), INITIAL_SIZE - (FRAGMENT_SIZE + 1));
    EXPECT_TRUE(io_handle_peer_->isWritable());
    EXPECT_EQ(io_handle_peer_->getWriteBuffer()->toString(), std::string(FRAGMENT_SIZE + 1, 'a'));
  }
  {
    // Write another fragment since when high watermark is reached.
    auto result1 = io_handle_->write(pending_data);
    EXPECT_TRUE(result1.ok());
    EXPECT_EQ(result1.return_value_, FRAGMENT_SIZE);
    EXPECT_EQ(pending_data.length(), INITIAL_SIZE - (FRAGMENT_SIZE + 1) - FRAGMENT_SIZE);
    EXPECT_FALSE(io_handle_peer_->isWritable());
    EXPECT_EQ(io_handle_peer_->getWriteBuffer()->toString(),
              std::string(2 * FRAGMENT_SIZE + 1, 'a'));
  }
  {
    // Confirm that the further write return `EAGAIN`.
    auto result2 = io_handle_->write(pending_data);
    ASSERT_EQ(result2.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
    ASSERT_EQ(result2.return_value_, 0);
  }
  {
    // Make the peer writable again.
    Buffer::OwnedImpl black_hole_buffer;
    auto result_drain =
        io_handle_peer_->read(black_hole_buffer, FRAGMENT_SIZE + FRAGMENT_SIZE / 2 + 2);
    ASSERT_EQ(result_drain.return_value_, FRAGMENT_SIZE + FRAGMENT_SIZE / 2 + 2);
    EXPECT_TRUE(io_handle_peer_->isWritable());
  }
  {
    // The buffer in peer is less than FRAGMENT_SIZE away from high watermark. Write a FRAGMENT_SIZE
    // anyway.
    auto len = io_handle_peer_->getWriteBuffer()->length();
    EXPECT_LT(io_handle_peer_->getWriteBuffer()->highWatermark() - len, FRAGMENT_SIZE);
    EXPECT_GT(pending_data.length(), FRAGMENT_SIZE);
    auto result3 = io_handle_->write(pending_data);
    EXPECT_EQ(result3.return_value_, FRAGMENT_SIZE);
    EXPECT_FALSE(io_handle_peer_->isWritable());
    EXPECT_EQ(io_handle_peer_->getWriteBuffer()->toString(), std::string(len + FRAGMENT_SIZE, 'a'));
  }
}

TEST_F(IoHandleImplTest, WriteErrorAfterShutdown) {
  Buffer::OwnedImpl buf("0123456789");
  // Write after shutdown.
  io_handle_->shutdown(ENVOY_SHUT_WR);
  auto result = io_handle_->write(buf);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::InvalidArgument);
  EXPECT_EQ(10, buf.length());
}

TEST_F(IoHandleImplTest, WriteErrorAfterClose) {
  Buffer::OwnedImpl buf("0123456789");
  io_handle_peer_->close();
  EXPECT_TRUE(io_handle_->isOpen());
  auto result = io_handle_->write(buf);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::InvalidArgument);
}

// Test writev return error code. Ignoring the side effect of event scheduling.
TEST_F(IoHandleImplTest, WritevAgain) {
  Buffer::OwnedImpl buf_to_write(std::string(256, ' '));
  io_handle_->write(buf_to_write);
  auto [guard, slice] = allocateOneSlice(128);
  io_handle_peer_->setWatermarks(128);
  auto result = io_handle_->writev(&slice, 1);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
}

TEST_F(IoHandleImplTest, PartialWritev) {
  io_handle_peer_->setWatermarks(128);
  Buffer::OwnedImpl pending_data("a");
  auto long_frag = Buffer::OwnedBufferFragmentImpl::create(
      std::string(255, 'b'),
      [](const Buffer::OwnedBufferFragmentImpl* fragment) { delete fragment; });
  auto tail_frag = Buffer::OwnedBufferFragmentImpl::create(
      "ccc", [](const Buffer::OwnedBufferFragmentImpl* fragment) { delete fragment; });
  pending_data.addBufferFragment(*long_frag.release());
  pending_data.addBufferFragment(*tail_frag.release());

  // Partial write: the first two slices are moved because the second slice move reaches the high
  // watermark.
  auto slices = pending_data.getRawSlices();
  EXPECT_EQ(3, slices.size());
  auto result = io_handle_->writev(slices.data(), slices.size());
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.return_value_, 256);
  pending_data.drain(result.return_value_);
  EXPECT_EQ(pending_data.length(), 3);
  EXPECT_FALSE(io_handle_peer_->isWritable());

  // Confirm that the further write return `EAGAIN`.
  auto slices2 = pending_data.getRawSlices();
  auto result2 = io_handle_->writev(slices2.data(), slices2.size());
  ASSERT_EQ(result2.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);

  // Make the peer writable again.
  Buffer::OwnedImpl black_hole_buffer;
  io_handle_peer_->read(black_hole_buffer, 10240);
  EXPECT_TRUE(io_handle_peer_->isWritable());
  auto slices3 = pending_data.getRawSlices();
  auto result3 = io_handle_->writev(slices3.data(), slices3.size());
  EXPECT_EQ(result3.return_value_, 3);
  pending_data.drain(result3.return_value_);
  EXPECT_EQ(0, pending_data.length());
}

TEST_F(IoHandleImplTest, WritevErrorAfterShutdown) {
  auto [guard, slice] = allocateOneSlice(128);
  // Writev after shutdown.
  io_handle_->shutdown(ENVOY_SHUT_WR);
  auto result = io_handle_->writev(&slice, 1);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::InvalidArgument);
}

TEST_F(IoHandleImplTest, WritevErrorAfterClose) {
  auto [guard, slice] = allocateOneSlice(1024);
  // Close the peer.
  io_handle_peer_->close();
  EXPECT_TRUE(io_handle_->isOpen());
  auto result = io_handle_->writev(&slice, 1);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::InvalidArgument);
}

TEST_F(IoHandleImplTest, WritevToPeer) {
  std::string raw_data("0123456789");
  absl::InlinedVector<Buffer::RawSlice, 4> slices{
      // Contains 1 byte.
      Buffer::RawSlice{static_cast<void*>(raw_data.data()), 1},
      // Contains 0 byte.
      Buffer::RawSlice{nullptr, 1},
      // Contains 0 byte.
      Buffer::RawSlice{raw_data.data() + 1, 0},
      // Contains 2 byte.
      Buffer::RawSlice{raw_data.data() + 1, 2},
  };
  io_handle_peer_->writev(slices.data(), slices.size());
  EXPECT_EQ(3, io_handle_->getWriteBuffer()->length());
  EXPECT_EQ("012", io_handle_->getWriteBuffer()->toString());
}

TEST_F(IoHandleImplTest, EventScheduleBasic) {
  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_,
      [this](uint32_t events) {
        cb_.called(events);
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);

  EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
  schedulable_cb->invokeCallback();
  io_handle_->resetFileEvents();
}

TEST_F(IoHandleImplTest, SetEnabledTriggerEventSchedule) {
  auto schedulable_cb = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  // No data is available to read. Will not schedule read.
  {
    SCOPED_TRACE("enable read but no readable.");
    EXPECT_CALL(*schedulable_cb, enabled());
    EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration()).Times(0);
    io_handle_->initializeFileEvent(
        dispatcher_,
        [this](uint32_t events) {
          cb_.called(events);
          return absl::OkStatus();
        },
        Event::FileTriggerType::Edge, Event::FileReadyType::Read);
    testing::Mock::VerifyAndClearExpectations(schedulable_cb);
  }
  {
    SCOPED_TRACE("enable readwrite but only writable.");
    EXPECT_CALL(*schedulable_cb, enabled());
    EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
    io_handle_->enableFileEvents(Event::FileReadyType::Read | Event::FileReadyType::Write);
    ASSERT_TRUE(schedulable_cb->enabled_);
    EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
    schedulable_cb->invokeCallback();
    ASSERT_FALSE(schedulable_cb->enabled_);
    testing::Mock::VerifyAndClearExpectations(schedulable_cb);
  }
  {
    SCOPED_TRACE("enable write and writable.");
    EXPECT_CALL(*schedulable_cb, enabled());
    EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
    io_handle_->enableFileEvents(Event::FileReadyType::Write);
    ASSERT_TRUE(schedulable_cb->enabled_);
    EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
    schedulable_cb->invokeCallback();
    ASSERT_FALSE(schedulable_cb->enabled_);
    testing::Mock::VerifyAndClearExpectations(schedulable_cb);
  }
  // Close io_handle_ first to prevent events originated from peer close.
  io_handle_->close();
  io_handle_peer_->close();
}

TEST_F(IoHandleImplTest, ReadAndWriteAreEdgeTriggered) {
  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_,
      [this](uint32_t events) {
        cb_.called(events);
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);

  EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
  schedulable_cb->invokeCallback();

  Buffer::OwnedImpl buf("abcd");
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_peer_->write(buf);

  EXPECT_CALL(cb_, called(Event::FileReadyType::Read));
  schedulable_cb->invokeCallback();

  // Drain 1 bytes.
  auto result = io_handle_->recv(buf_.data(), 1, 0);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(1, result.return_value_);

  ASSERT_FALSE(schedulable_cb->enabled_);
  io_handle_->resetFileEvents();
}

TEST_F(IoHandleImplTest, SetDisabledBlockEventSchedule) {
  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_,
      [this](uint32_t events) {
        cb_.called(events);
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Write);
  ASSERT_TRUE(schedulable_cb->enabled_);

  // The write event is cleared and the read event is not ready.
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, cancel());
  io_handle_->enableFileEvents(Event::FileReadyType::Read);
  testing::Mock::VerifyAndClearExpectations(schedulable_cb);

  ASSERT_FALSE(schedulable_cb->enabled_);
  io_handle_->resetFileEvents();
}

TEST_F(IoHandleImplTest, EventResetClearCallback) {
  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_,
      [this](uint32_t events) {
        cb_.called(events);
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Write);
  ASSERT_TRUE(schedulable_cb->enabled_);
  io_handle_->resetFileEvents();
}

TEST_F(IoHandleImplTest, DrainToLowWaterMarkTriggerReadEvent) {
  io_handle_->setWatermarks(128);

  EXPECT_FALSE(io_handle_->isReadable());
  EXPECT_TRUE(io_handle_peer_->isWritable());

  Buffer::OwnedImpl buf_to_write(std::string(256, 'a'));
  io_handle_peer_->write(buf_to_write);

  EXPECT_TRUE(io_handle_->isReadable());
  EXPECT_FALSE(io_handle_->isWritable());

  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  // No event is available.
  EXPECT_CALL(*schedulable_cb, cancel());
  io_handle_peer_->initializeFileEvent(
      dispatcher_,
      [this](uint32_t events) {
        cb_.called(events);
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
  // Neither readable nor writable.
  ASSERT_FALSE(schedulable_cb->enabled_);

  {
    SCOPED_TRACE("drain very few data.");
    auto result = io_handle_->recv(buf_.data(), 1, 0);
    EXPECT_FALSE(io_handle_->isWritable());
  }
  {
    SCOPED_TRACE("drain to low watermark.");
    EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
    auto result = io_handle_->recv(buf_.data(), 232, 0);
    EXPECT_TRUE(io_handle_->isWritable());
    EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
    schedulable_cb->invokeCallback();
  }
  {
    SCOPED_TRACE("clean up.");
    EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
    // Important: close before peer.
    io_handle_->close();
  }
}

TEST_F(IoHandleImplTest, Close) {
  Buffer::OwnedImpl buf_to_write("abcd");
  io_handle_peer_->write(buf_to_write);
  std::string accumulator;
  schedulable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  io_handle_->initializeFileEvent(
      dispatcher_,
      [this, &should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          while (true) {
            auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
            if (result.ok()) {
              // Read EOF.
              if (result.return_value_ == 0) {
                should_close = true;
                break;
              } else {
                accumulator += std::string(buf_.data(), result.return_value_);
              }
            } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
              ENVOY_LOG_MISC(debug, "read returns EAGAIN");
              break;
            } else {
              ENVOY_LOG_MISC(debug, "will close");
              should_close = true;
              break;
            }
          }
        }
        if (events & Event::FileReadyType::Write) {
          Buffer::OwnedImpl buf("");
          auto result = io_handle_->write(buf);
          if (!result.ok() && result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
            should_close = true;
          }
        }
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
  schedulable_cb_->invokeCallback();

  // Not closed yet.
  ASSERT_FALSE(should_close);

  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->close();

  ASSERT_TRUE(schedulable_cb_->enabled());
  schedulable_cb_->invokeCallback();
  ASSERT_TRUE(should_close);

  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration()).Times(0);
  io_handle_->close();
  EXPECT_EQ(4, accumulator.size());
  io_handle_->resetFileEvents();
}

// Test that a readable event is raised when peer shutdown write. Also confirm read will return
// EAGAIN.
TEST_F(IoHandleImplTest, ShutDownRaiseEvent) {
  Buffer::OwnedImpl buf_to_write("abcd");
  io_handle_peer_->write(buf_to_write);

  std::string accumulator;
  schedulable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  io_handle_->initializeFileEvent(
      dispatcher_,
      [this, &should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
          if (result.ok()) {
            accumulator += std::string(buf_.data(), result.return_value_);
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  schedulable_cb_->invokeCallback();

  // Not closed yet.
  ASSERT_FALSE(should_close);

  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->shutdown(ENVOY_SHUT_WR);

  ASSERT_TRUE(schedulable_cb_->enabled());
  schedulable_cb_->invokeCallback();
  ASSERT_FALSE(should_close);
  EXPECT_EQ(4, accumulator.size());
  io_handle_->close();
  io_handle_->resetFileEvents();
}

TEST_F(IoHandleImplTest, WriteScheduleWritableEvent) {
  std::string accumulator;
  schedulable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  io_handle_->initializeFileEvent(
      dispatcher_,
      [&should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          Buffer::OwnedImpl buf;
          auto reservation = buf.reserveSingleSlice(1024);
          auto slice = reservation.slice();
          auto result = handle->readv(1024, &slice, 1);
          if (result.ok()) {
            accumulator += std::string(static_cast<char*>(slice.mem_), result.return_value_);
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
  schedulable_cb_->invokeCallback();
  EXPECT_FALSE(schedulable_cb_->enabled());

  Buffer::OwnedImpl data_to_write("0123456789");
  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->write(data_to_write);
  EXPECT_EQ(0, data_to_write.length());

  EXPECT_TRUE(schedulable_cb_->enabled());
  schedulable_cb_->invokeCallback();
  EXPECT_EQ("0123456789", accumulator);
  EXPECT_FALSE(should_close);

  io_handle_->close();
}

TEST_F(IoHandleImplTest, WritevScheduleWritableEvent) {
  std::string accumulator;
  schedulable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  io_handle_->initializeFileEvent(
      dispatcher_,
      [&should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          Buffer::OwnedImpl buf;
          auto reservation = buf.reserveSingleSlice(1024);
          auto slice = reservation.slice();
          auto result = handle->readv(1024, &slice, 1);
          if (result.ok()) {
            accumulator += std::string(static_cast<char*>(slice.mem_), result.return_value_);
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read | Event::FileReadyType::Write);
  schedulable_cb_->invokeCallback();
  EXPECT_FALSE(schedulable_cb_->enabled());

  std::string raw_data("0123456789");
  Buffer::RawSlice slice{static_cast<void*>(raw_data.data()), raw_data.size()};
  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->writev(&slice, 1);

  EXPECT_TRUE(schedulable_cb_->enabled());
  schedulable_cb_->invokeCallback();
  EXPECT_EQ("0123456789", accumulator);
  EXPECT_FALSE(should_close);

  io_handle_->close();
}

TEST_F(IoHandleImplTest, ReadAfterShutdownWrite) {
  io_handle_peer_->shutdown(ENVOY_SHUT_WR);
  ENVOY_LOG_MISC(debug, "after {} shutdown write ", static_cast<void*>(io_handle_peer_.get()));
  std::string accumulator;
  schedulable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  bool should_close = false;
  io_handle_peer_->initializeFileEvent(
      dispatcher_,
      [&should_close, handle = io_handle_peer_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          Buffer::OwnedImpl buf;
          auto reservation = buf.reserveSingleSlice(1024);
          auto slice = reservation.slice();
          auto result = handle->readv(1024, &slice, 1);
          if (result.ok()) {
            if (result.return_value_ == 0) {
              should_close = true;
            } else {
              accumulator += std::string(static_cast<char*>(slice.mem_), result.return_value_);
            }
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);

  EXPECT_FALSE(schedulable_cb_->enabled());
  std::string raw_data("0123456789");
  Buffer::RawSlice slice{static_cast<void*>(raw_data.data()), raw_data.size()};
  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  io_handle_->writev(&slice, 1);
  EXPECT_TRUE(schedulable_cb_->enabled());

  schedulable_cb_->invokeCallback();
  EXPECT_FALSE(schedulable_cb_->enabled());
  EXPECT_EQ(raw_data, accumulator);

  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  io_handle_->close();
  io_handle_->resetFileEvents();
}

TEST_F(IoHandleImplTest, NotifyWritableAfterShutdownWrite) {
  io_handle_peer_->setWatermarks(128);

  Buffer::OwnedImpl buf(std::string(256, 'a'));
  io_handle_->write(buf);
  EXPECT_FALSE(io_handle_peer_->isWritable());

  io_handle_->shutdown(ENVOY_SHUT_WR);
  ENVOY_LOG_MISC(debug, "after {} shutdown write", static_cast<void*>(io_handle_.get()));

  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_peer_->initializeFileEvent(
      dispatcher_,
      [this](uint32_t events) {
        cb_.called(events);
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  EXPECT_CALL(cb_, called(Event::FileReadyType::Read));
  schedulable_cb->invokeCallback();
  EXPECT_FALSE(schedulable_cb->enabled_);

  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration()).Times(0);
  auto result = io_handle_peer_->recv(buf_.data(), buf_.size(), 0);
  EXPECT_EQ(256, result.return_value_);
  // Readable event is not activated due to edge trigger type.
  EXPECT_FALSE(schedulable_cb->enabled_);

  // The `end of stream` is delivered.
  auto result_at_eof = io_handle_peer_->recv(buf_.data(), buf_.size(), 0);
  EXPECT_EQ(0, result_at_eof.return_value_);

  // Also confirm `EOS` can triggered read ready event.
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_peer_->enableFileEvents(Event::FileReadyType::Read);
  EXPECT_CALL(cb_, called(Event::FileReadyType::Read));
  schedulable_cb->invokeCallback();

  io_handle_peer_->close();
}

TEST_F(IoHandleImplTest, ReturnValidInternalAddress) {
  const auto& local_address = io_handle_->localAddress();
  ASSERT_NE(nullptr, local_address);
  ASSERT_EQ(nullptr, local_address->ip());
  ASSERT_EQ(nullptr, local_address->pipe());
  ASSERT_NE(nullptr, local_address->envoyInternalAddress());
  const auto& remote_address = io_handle_->peerAddress();
  ASSERT_NE(nullptr, remote_address);
  ASSERT_EQ(nullptr, remote_address->ip());
  ASSERT_EQ(nullptr, remote_address->pipe());
  ASSERT_NE(nullptr, remote_address->envoyInternalAddress());
}

TEST_F(IoHandleImplTest, NotSupportingMmsg) { EXPECT_FALSE(io_handle_->supportsMmsg()); }

TEST_F(IoHandleImplTest, NotSupportsUdpGro) { EXPECT_FALSE(io_handle_->supportsUdpGro()); }

TEST_F(IoHandleImplTest, DomainNullOpt) { EXPECT_FALSE(io_handle_->domain().has_value()); }

TEST_F(IoHandleImplTest, Connect) {
  auto address_is_ignored =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_id");
  EXPECT_EQ(0, io_handle_->connect(address_is_ignored).return_value_);

  // Below is emulation of the connect().
  int immediate_error_value = -1;
  socklen_t error_value_len = 0;
  EXPECT_EQ(0, io_handle_->getOption(SOL_SOCKET, SO_ERROR, &immediate_error_value, &error_value_len)
                   .return_value_);
  EXPECT_EQ(sizeof(int), error_value_len);
  EXPECT_EQ(CONNECTED, immediate_error_value);

  // If the peer shutdown write but not yet closes, this io_handle should consider it
  // as connected because the socket may be readable.
  immediate_error_value = -1;
  error_value_len = 0;
  EXPECT_EQ(io_handle_peer_->shutdown(ENVOY_SHUT_WR).return_value_, 0);
  EXPECT_EQ(0, io_handle_->getOption(SOL_SOCKET, SO_ERROR, &immediate_error_value, &error_value_len)
                   .return_value_);
  EXPECT_EQ(sizeof(int), error_value_len);
  EXPECT_EQ(CONNECTED, immediate_error_value);
}

TEST_F(IoHandleImplTest, ConnectToClosedIoHandle) {
  auto address_is_ignored =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_id");
  io_handle_peer_->close();
  auto result = io_handle_->connect(address_is_ignored);
  EXPECT_EQ(-1, result.return_value_);
  EXPECT_EQ(SOCKET_ERROR_INVAL, result.errno_);

  // Below is emulation of the connect().
  int immediate_error_value = -1;
  socklen_t error_value_len = 0;
  EXPECT_EQ(0, io_handle_->getOption(SOL_SOCKET, SO_ERROR, &immediate_error_value, &error_value_len)
                   .return_value_);
  EXPECT_EQ(sizeof(int), error_value_len);
  EXPECT_NE(CONNECTED, immediate_error_value);
}

TEST_F(IoHandleImplTest, ActivateEvent) {
  schedulable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  io_handle_->initializeFileEvent(
      dispatcher_, [&, handle = io_handle_.get()](uint32_t) { return absl::OkStatus(); },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  EXPECT_FALSE(schedulable_cb_->enabled());
  io_handle_->activateFileEvents(Event::FileReadyType::Read);
  ASSERT_TRUE(schedulable_cb_->enabled());
}

// This is a compatibility test for Envoy Connection. When a connection is destroyed, the Envoy
// connection may close the underlying handle but not destroy that io handle. Meanwhile, the
// Connection object does not expect any further event be invoked because the connection in destroy
// pending state can not support read/write.
TEST_F(IoHandleImplTest, EventCallbackIsNotInvokedIfHandleIsClosed) {
  testing::MockFunction<void()> check_event_cb;
  testing::MockFunction<void()> check_schedulable_cb_destroyed;

  schedulable_cb_ =
      new NiceMock<Event::MockSchedulableCallback>(&dispatcher_, &check_schedulable_cb_destroyed);
  io_handle_->initializeFileEvent(
      dispatcher_,
      [&, handle = io_handle_.get()](uint32_t) {
        check_event_cb.Call();
        return absl::OkStatus();
      },
      Event::FileTriggerType::Edge, Event::FileReadyType::Read);
  EXPECT_FALSE(schedulable_cb_->enabled());
  io_handle_->activateFileEvents(Event::FileReadyType::Read);
  EXPECT_TRUE(schedulable_cb_->enabled());

  {
    EXPECT_CALL(check_event_cb, Call()).Times(0);
    EXPECT_CALL(check_schedulable_cb_destroyed, Call());
    io_handle_->close();
    // Verify that the schedulable_cb is destroyed along with close(), not later.
    testing::Mock::VerifyAndClearExpectations(&check_schedulable_cb_destroyed);
  }
}

TEST_F(IoHandleImplTest, DeathOnActivatingDestroyedEvents) {
  io_handle_->resetFileEvents();
  ASSERT_DEBUG_DEATH(io_handle_->activateFileEvents(Event::FileReadyType::Read),
                     "Null user_file_event_");
}

TEST_F(IoHandleImplTest, DeathOnEnablingDestroyedEvents) {
  io_handle_->resetFileEvents();
  ASSERT_DEBUG_DEATH(io_handle_->enableFileEvents(Event::FileReadyType::Read),
                     "Null user_file_event_");
}

TEST_F(IoHandleImplTest, NotImplementDuplicate) { EXPECT_ENVOY_BUG(io_handle_->duplicate(), ""); }

TEST_F(IoHandleImplTest, NotImplementAccept) {
  EXPECT_ENVOY_BUG(io_handle_->accept(nullptr, nullptr), "");
}

TEST_F(IoHandleImplTest, LastRoundtripTimeNullOpt) {
  ASSERT_EQ(absl::nullopt, io_handle_->lastRoundTripTime());
}

// IoHandleImpl can support EmulatedEdge trigger type but not level trigger type.
TEST_F(IoHandleImplTest, CreatePlatformDefaultTriggerTypeFailOnWindows) {
  // schedulable_cb will be destroyed by IoHandle.
  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, cancel());
  io_handle_->initializeFileEvent(
      dispatcher_,
      [this](uint32_t events) {
        cb_.called(events);
        return absl::OkStatus();
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  io_handle_->close();
  io_handle_peer_->close();
}

class TestObject : public StreamInfo::FilterState::Object {
public:
  TestObject(int value) : value_(value) {}
  int value_;
};

TEST_F(IoHandleImplTest, PassthroughState) {
  auto source_metadata = std::make_unique<envoy::config::core::v3::Metadata>();
  ProtobufWkt::Struct& map = (*source_metadata->mutable_filter_metadata())["envoy.test"];
  ProtobufWkt::Value val;
  val.set_string_value("val");
  (*map.mutable_fields())["key"] = val;
  StreamInfo::FilterState::Objects source_filter_state;
  auto object = std::make_shared<TestObject>(1000);
  source_filter_state.push_back(
      {object, StreamInfo::FilterState::StateType::ReadOnly,
       StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection, "object_key"});
  ASSERT_NE(nullptr, io_handle_->passthroughState());
  io_handle_->passthroughState()->initialize(std::move(source_metadata), source_filter_state);

  StreamInfo::FilterStateImpl dest_filter_state(StreamInfo::FilterState::LifeSpan::Connection);
  envoy::config::core::v3::Metadata dest_metadata;
  ASSERT_NE(nullptr, io_handle_peer_->passthroughState());
  io_handle_peer_->passthroughState()->mergeInto(dest_metadata, dest_filter_state);
  ASSERT_EQ("val",
            dest_metadata.filter_metadata().at("envoy.test").fields().at("key").string_value());
  auto dest_object = dest_filter_state.getDataReadOnly<TestObject>("object_key");
  ASSERT_NE(nullptr, dest_object);
  ASSERT_EQ(object->value_, dest_object->value_);
}

class IoHandleImplNotImplementedTest : public testing::Test {
public:
  IoHandleImplNotImplementedTest() {
    std::tie(io_handle_, io_handle_peer_) = IoHandleFactory::createIoHandlePair();
  }

  ~IoHandleImplNotImplementedTest() override {
    if (io_handle_->isOpen()) {
      io_handle_->close();
    }
    if (io_handle_peer_->isOpen()) {
      io_handle_peer_->close();
    }
  }

  std::unique_ptr<IoHandleImpl> io_handle_;
  std::unique_ptr<IoHandleImpl> io_handle_peer_;
  Buffer::RawSlice slice_;
};

TEST_F(IoHandleImplNotImplementedTest, ErrorOnSetBlocking) {
  EXPECT_THAT(io_handle_->setBlocking(false), IsNotSupportedResult());
  EXPECT_THAT(io_handle_->setBlocking(true), IsNotSupportedResult());
}

TEST_F(IoHandleImplNotImplementedTest, ErrorOnSendmsg) {
  EXPECT_THAT(io_handle_->sendmsg(&slice_, 0, 0, nullptr,
                                  Network::Address::EnvoyInternalInstance("listener_id")),
              IsInvalidAddress());
}

TEST_F(IoHandleImplNotImplementedTest, ErrorOnRecvmsg) {
  Network::IoHandle::RecvMsgOutput output_is_ignored(1, nullptr);
  EXPECT_THAT(io_handle_->recvmsg(&slice_, 0, 0, {}, output_is_ignored), IsInvalidAddress());
}

TEST_F(IoHandleImplNotImplementedTest, ErrorOnRecvmmsg) {
  RawSliceArrays slices_is_ignored(1, absl::FixedArray<Buffer::RawSlice>({slice_}));
  Network::IoHandle::RecvMsgOutput output_is_ignored(1, nullptr);
  EXPECT_THAT(io_handle_->recvmmsg(slices_is_ignored, 0, {}, output_is_ignored),
              IsInvalidAddress());
}

TEST_F(IoHandleImplNotImplementedTest, ErrorOnBind) {
  auto address_is_ignored =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_id");
  EXPECT_THAT(io_handle_->bind(address_is_ignored), IsNotSupportedResult());
}

TEST_F(IoHandleImplNotImplementedTest, ErrorOnListen) {
  int back_log_is_ignored = 0;
  EXPECT_THAT(io_handle_->listen(back_log_is_ignored), IsNotSupportedResult());
}

TEST_F(IoHandleImplNotImplementedTest, ErrorOnSetOption) {
  EXPECT_THAT(io_handle_->setOption(0, 0, nullptr, 0), IsNotSupportedResult());
}

TEST_F(IoHandleImplNotImplementedTest, ErrorOnGetOption) {
  EXPECT_THAT(io_handle_->getOption(0, 0, nullptr, nullptr), IsNotSupportedResult());
}

TEST_F(IoHandleImplNotImplementedTest, ErrorOnIoctl) {
  EXPECT_THAT(io_handle_->ioctl(0, nullptr, 0, nullptr, 0, nullptr), IsNotSupportedResult());
}
} // namespace
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
