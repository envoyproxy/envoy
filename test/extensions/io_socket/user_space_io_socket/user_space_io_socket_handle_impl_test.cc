#include "envoy/buffer/buffer.h"
#include "envoy/event/file_event.h"

#include "common/buffer/buffer_impl.h"
#include "common/common/fancy_logger.h"
#include "common/network/address_impl.h"

#include "extensions/io_socket/user_space_io_socket/user_space_io_socket_handle_impl.h"

#include "test/mocks/event/mocks.h"

#include "absl/container/fixed_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpaceIoSocket {
namespace {

MATCHER(IsInvalidAddress, "") {
  return arg.err_->getErrorCode() == Api::IoError::IoErrorCode::NoSupport;
}

MATCHER(IsNotSupportedResult, "") { return arg.errno_ == SOCKET_ERROR_NOT_SUP; }

ABSL_MUST_USE_RESULT std::pair<Buffer::Slice, Buffer::RawSlice> allocateOneSlice(uint64_t size) {
  Buffer::Slice mutable_slice(size);
  auto slice = mutable_slice.reserve(size);
  EXPECT_NE(nullptr, slice.mem_);
  EXPECT_EQ(size, slice.len_);
  return {std::move(mutable_slice), slice};
}

class MockFileEventCallback {
public:
  MOCK_METHOD(void, called, (uint32_t arg));
};

class UserSpaceIoSocketHandleTest : public testing::Test {
public:
  UserSpaceIoSocketHandleTest() : buf_(1024) {
    io_handle_ = std::make_unique<UserSpaceIoSocketHandleImpl>();
    io_handle_peer_ = std::make_unique<UserSpaceIoSocketHandleImpl>();
    io_handle_->setPeerHandle(io_handle_peer_.get());
    io_handle_peer_->setPeerHandle(io_handle_.get());
  }

  ~UserSpaceIoSocketHandleTest() override = default;

  Buffer::WatermarkBuffer& getWatermarkBufferHelper(UserSpaceIoSocketHandleImpl& io_handle) {
    return dynamic_cast<Buffer::WatermarkBuffer&>(*io_handle.getWriteBuffer());
  }

  NiceMock<Event::MockDispatcher> dispatcher_;

  // Owned by UserSpaceIoSocketHandle.
  NiceMock<Event::MockSchedulableCallback>* schedulable_cb_;
  MockFileEventCallback cb_;
  std::unique_ptr<UserSpaceIoSocketHandleImpl> io_handle_;
  std::unique_ptr<UserSpaceIoSocketHandleImpl> io_handle_peer_;
  absl::FixedArray<char> buf_;
};

// Test recv side effects.
TEST_F(UserSpaceIoSocketHandleTest, BasicRecv) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("0123456789");
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    ASSERT_EQ(10, result.rc_);
    ASSERT_EQ("0123456789", absl::string_view(buf_.data(), result.rc_));
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
TEST_F(UserSpaceIoSocketHandleTest, RecvPeek) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("0123456789");
  {
    ::memset(buf_.data(), 1, buf_.size());
    auto result = io_handle_->recv(buf_.data(), 5, MSG_PEEK);
    ASSERT_EQ(5, result.rc_);
    ASSERT_EQ("01234", absl::string_view(buf_.data(), result.rc_));
    // The data beyond the boundary is untouched.
    ASSERT_EQ(std::string(buf_.size() - 5, 1), absl::string_view(buf_.data() + 5, buf_.size() - 5));
  }
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
    ASSERT_EQ(10, result.rc_);
    ASSERT_EQ("0123456789", absl::string_view(buf_.data(), result.rc_));
  }
  {
    // Drain the pending buffer.
    auto recv_result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    EXPECT_TRUE(recv_result.ok());
    EXPECT_EQ(10, recv_result.rc_);
    ASSERT_EQ("0123456789", absl::string_view(buf_.data(), recv_result.rc_));
    auto peek_result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    // `EAGAIN`.
    EXPECT_FALSE(peek_result.ok());
    EXPECT_EQ(Api::IoError::IoErrorCode::Again, peek_result.err_->getErrorCode());
  }
  {
    // Peek upon shutdown.
    io_handle_->setWriteEnd();
    auto result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
    EXPECT_EQ(0, result.rc_);
    ASSERT(result.ok());
  }
}

TEST_F(UserSpaceIoSocketHandleTest, RecvPeekWhenPendingDataButShutdown) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("0123456789");
  auto result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
  ASSERT_EQ(10, result.rc_);
  ASSERT_EQ("0123456789", absl::string_view(buf_.data(), result.rc_));
}

TEST_F(UserSpaceIoSocketHandleTest, MultipleRecvDrain) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("abcd");
  {
    auto result = io_handle_->recv(buf_.data(), 1, 0);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(1, result.rc_);
    EXPECT_EQ("a", absl::string_view(buf_.data(), 1));
  }
  {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(3, result.rc_);

    EXPECT_EQ("bcd", absl::string_view(buf_.data(), 3));
    EXPECT_EQ(0, internal_buffer.length());
  }
}

// Test read side effects.
TEST_F(UserSpaceIoSocketHandleTest, ReadEmpty) {
  Buffer::OwnedImpl buf;
  auto result = io_handle_->read(buf, 10);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  io_handle_->setWriteEnd();
  result = io_handle_->read(buf, 10);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(0, result.rc_);
}

// Test read side effects.
TEST_F(UserSpaceIoSocketHandleTest, ReadContent) {
  Buffer::OwnedImpl buf;
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("abcdefg");
  auto result = io_handle_->read(buf, 3);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(3, result.rc_);
  ASSERT_EQ(3, buf.length());
  ASSERT_EQ(4, internal_buffer.length());
  result = io_handle_->read(buf, 10);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(4, result.rc_);
  ASSERT_EQ(7, buf.length());
  ASSERT_EQ(0, internal_buffer.length());
}

// Test readv behavior.
TEST_F(UserSpaceIoSocketHandleTest, BasicReadv) {
  Buffer::OwnedImpl buf_to_write("abc");
  io_handle_peer_->write(buf_to_write);

  Buffer::OwnedImpl buf;
  Buffer::RawSlice slice;
  buf.reserve(1024, &slice, 1);
  auto result = io_handle_->readv(1024, &slice, 1);

  EXPECT_TRUE(result.ok());
  EXPECT_EQ(3, result.rc_);

  result = io_handle_->readv(1024, &slice, 1);

  EXPECT_FALSE(result.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());

  io_handle_->setWriteEnd();
  result = io_handle_->readv(1024, &slice, 1);
  // EOF
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(0, result.rc_);
}

TEST_F(UserSpaceIoSocketHandleTest, FlowControl) {
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
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  while (internal_buffer.length() > 0) {
    SCOPED_TRACE(internal_buffer.length());
    FANCY_LOG(debug, "internal buffer length = {}", internal_buffer.length());
    EXPECT_TRUE(io_handle_->isReadable());
    bool writable = io_handle_->isWritable();
    FANCY_LOG(debug, "internal buffer length = {}, writable = {}", internal_buffer.length(),
              writable);
    if (writable) {
      writable_flipped = true;
    } else {
      ASSERT_FALSE(writable_flipped);
    }
    auto result = io_handle_->recv(buf_.data(), 32, 0);
    EXPECT_TRUE(result.ok());
    EXPECT_EQ(32, result.rc_);
  }
  ASSERT_EQ(0, internal_buffer.length());
  ASSERT_TRUE(writable_flipped);

  // Finally the buffer is empty.
  EXPECT_FALSE(io_handle_->isReadable());
  EXPECT_TRUE(io_handle_->isWritable());
}

// Consistent with other IoHandle: allow write empty data when handle is closed.
TEST_F(UserSpaceIoSocketHandleTest, NoErrorWriteZeroDataToClosedIoHandle) {
  io_handle_->close();
  {
    Buffer::OwnedImpl buf;
    auto result = io_handle_->write(buf);
    ASSERT_EQ(0, result.rc_);
    ASSERT(result.ok());
  }
  {
    Buffer::RawSlice slice{nullptr, 0};
    auto result = io_handle_->writev(&slice, 1);
    ASSERT_EQ(0, result.rc_);
    ASSERT(result.ok());
  }
}

TEST_F(UserSpaceIoSocketHandleTest, ErrorOnClosedIoHandle) {
  io_handle_->close();
  {
    auto [guard, slice] = allocateOneSlice(1024);
    auto result = io_handle_->recv(slice.mem_, slice.len_, 0);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  }
  {
    Buffer::OwnedImpl buf;
    auto result = io_handle_->read(buf, 10);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  }
  {
    auto [guard, slice] = allocateOneSlice(1024);
    auto result = io_handle_->readv(1024, &slice, 1);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  }
  {
    Buffer::OwnedImpl buf("0123456789");
    auto result = io_handle_->write(buf);
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  }
  {
    Buffer::OwnedImpl buf("0123456789");
    auto slices = buf.getRawSlices();
    ASSERT(!slices.empty());
    auto result = io_handle_->writev(slices.data(), slices.size());
    ASSERT(!result.ok());
    ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
  }
}

TEST_F(UserSpaceIoSocketHandleTest, RepeatedShutdownWR) {
  EXPECT_EQ(io_handle_peer_->shutdown(ENVOY_SHUT_WR).rc_, 0);
  EXPECT_EQ(io_handle_peer_->shutdown(ENVOY_SHUT_WR).rc_, 0);
}

TEST_F(UserSpaceIoSocketHandleTest, ShutDownOptionsNotSupported) {
  ASSERT_DEBUG_DEATH(io_handle_peer_->shutdown(ENVOY_SHUT_RD), "");
  ASSERT_DEBUG_DEATH(io_handle_peer_->shutdown(ENVOY_SHUT_RDWR), "");
}

TEST_F(UserSpaceIoSocketHandleTest, WriteByMove) {
  Buffer::OwnedImpl buf("0123456789");
  auto result = io_handle_peer_->write(buf);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(10, result.rc_);
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  EXPECT_EQ("0123456789", internal_buffer.toString());
  EXPECT_EQ(0, buf.length());
}

// Test write return error code. Ignoring the side effect of event scheduling.
TEST_F(UserSpaceIoSocketHandleTest, WriteAgain) {
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

// Test write() moves the fragments in front until the destination is over high watermark.
TEST_F(UserSpaceIoSocketHandleTest, PartialWrite) {
  // Populate write destination with massive data so as to not writable.
  io_handle_peer_->setWatermarks(128);
  // Fragment contents                | a |`bbbb...b`|`ccc`|
  // Len per fragment                 | 1 |  255     |  3  |
  // Watermark boundary at b area     | low | high         |
  // Write                            | 1st          | 2nd |
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
  auto result = io_handle_->write(pending_data);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(result.rc_, 256);
  EXPECT_EQ(pending_data.length(), 3);
  EXPECT_FALSE(io_handle_peer_->isWritable());

  // Confirm that the further write return `EAGAIN`.
  auto result2 = io_handle_->write(pending_data);
  ASSERT_EQ(result2.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);

  // Make the peer writable again.
  Buffer::OwnedImpl black_hole_buffer;
  io_handle_peer_->read(black_hole_buffer, 10240);
  EXPECT_TRUE(io_handle_peer_->isWritable());
  auto result3 = io_handle_->write(pending_data);
  EXPECT_EQ(result3.rc_, 3);
  EXPECT_EQ(0, pending_data.length());
}

TEST_F(UserSpaceIoSocketHandleTest, WriteErrorAfterShutdown) {
  Buffer::OwnedImpl buf("0123456789");
  // Write after shutdown.
  io_handle_->shutdown(ENVOY_SHUT_WR);
  auto result = io_handle_->write(buf);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
  EXPECT_EQ(10, buf.length());
}

TEST_F(UserSpaceIoSocketHandleTest, WriteErrorAfterClose) {
  Buffer::OwnedImpl buf("0123456789");
  io_handle_peer_->close();
  EXPECT_TRUE(io_handle_->isOpen());
  auto result = io_handle_->write(buf);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
}

// Test writev return error code. Ignoring the side effect of event scheduling.
TEST_F(UserSpaceIoSocketHandleTest, WritevAgain) {
  auto [guard, slice] = allocateOneSlice(128);
  // Populate write destination with massive data so as to not writable.
  io_handle_peer_->setWatermarks(128);
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_peer_);
  internal_buffer.add(std::string(256, ' '));
  auto result = io_handle_->writev(&slice, 1);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
}

// Test writev() copies the slices in front until the destination is over high watermark.
TEST_F(UserSpaceIoSocketHandleTest, PartialWritev) {
  // Populate write destination with massive data so as to not writable.
  io_handle_peer_->setWatermarks(128);
  // Slices contents                  | a |`bbbb...b`|`ccc`|
  // Len per slice                    | 1 |  255     |  3  |
  // Watermark boundary at b area     | low | high         |
  // Writev                           | 1st          | 2nd |
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
  EXPECT_EQ(result.rc_, 256);
  pending_data.drain(result.rc_);
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
  EXPECT_EQ(result3.rc_, 3);
  pending_data.drain(result3.rc_);
  EXPECT_EQ(0, pending_data.length());
}

TEST_F(UserSpaceIoSocketHandleTest, WritevErrorAfterShutdown) {
  auto [guard, slice] = allocateOneSlice(128);
  // Writev after shutdown.
  io_handle_->shutdown(ENVOY_SHUT_WR);
  auto result = io_handle_->writev(&slice, 1);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
}

TEST_F(UserSpaceIoSocketHandleTest, WritevErrorAfterClose) {
  auto [guard, slice] = allocateOneSlice(1024);
  // Close the peer.
  io_handle_peer_->close();
  EXPECT_TRUE(io_handle_->isOpen());
  auto result = io_handle_->writev(&slice, 1);
  ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
}

TEST_F(UserSpaceIoSocketHandleTest, WritevToPeer) {
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
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  EXPECT_EQ(3, internal_buffer.length());
  EXPECT_EQ("012", internal_buffer.toString());
}

TEST_F(UserSpaceIoSocketHandleTest, EventScheduleBasic) {
  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write);

  EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
  schedulable_cb->invokeCallback();
  io_handle_->resetFileEvents();
}

TEST_F(UserSpaceIoSocketHandleTest, SetEnabledTriggerEventSchedule) {
  auto schedulable_cb = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  // No data is available to read. Will not schedule read.
  {
    SCOPED_TRACE("enable read but no readable.");
    EXPECT_CALL(*schedulable_cb, enabled());
    EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration()).Times(0);
    io_handle_->initializeFileEvent(
        dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
        Event::FileReadyType::Read);
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

TEST_F(UserSpaceIoSocketHandleTest, ReadAndWriteAreEdgeTriggered) {
  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write);

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
  EXPECT_EQ(1, result.rc_);

  ASSERT_FALSE(schedulable_cb->enabled_);
  io_handle_->resetFileEvents();
}

TEST_F(UserSpaceIoSocketHandleTest, SetDisabledBlockEventSchedule) {
  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Write);
  ASSERT_TRUE(schedulable_cb->enabled_);

  // The write event is cleared and the read event is not ready.
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, cancel());
  io_handle_->enableFileEvents(Event::FileReadyType::Read);
  testing::Mock::VerifyAndClearExpectations(schedulable_cb);

  ASSERT_FALSE(schedulable_cb->enabled_);
  io_handle_->resetFileEvents();
}

TEST_F(UserSpaceIoSocketHandleTest, EventResetClearCallback) {
  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Write);
  ASSERT_TRUE(schedulable_cb->enabled_);
  io_handle_->resetFileEvents();
}

TEST_F(UserSpaceIoSocketHandleTest, DrainToLowWaterMarkTriggerReadEvent) {
  io_handle_->setWatermarks(128);
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);

  EXPECT_FALSE(io_handle_->isReadable());
  EXPECT_TRUE(io_handle_peer_->isWritable());

  std::string big_chunk(256, 'a');
  internal_buffer.add(big_chunk);
  EXPECT_TRUE(io_handle_->isReadable());
  EXPECT_FALSE(io_handle_->isWritable());

  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  // No event is available.
  EXPECT_CALL(*schedulable_cb, cancel());
  io_handle_peer_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read | Event::FileReadyType::Write);
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

TEST_F(UserSpaceIoSocketHandleTest, Close) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("abcd");
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
              if (result.rc_ == 0) {
                should_close = true;
                break;
              } else {
                accumulator += absl::string_view(buf_.data(), result.rc_);
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
TEST_F(UserSpaceIoSocketHandleTest, ShutDownRaiseEvent) {
  auto& internal_buffer = getWatermarkBufferHelper(*io_handle_);
  internal_buffer.add("abcd");

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
            accumulator += absl::string_view(buf_.data(), result.rc_);
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
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

TEST_F(UserSpaceIoSocketHandleTest, WriteScheduleWritableEvent) {
  std::string accumulator;
  schedulable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  io_handle_->initializeFileEvent(
      dispatcher_,
      [&should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          Buffer::OwnedImpl buf;
          Buffer::RawSlice slice;
          buf.reserve(1024, &slice, 1);
          auto result = handle->readv(1024, &slice, 1);
          if (result.ok()) {
            accumulator += absl::string_view(static_cast<char*>(slice.mem_), result.rc_);
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
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

TEST_F(UserSpaceIoSocketHandleTest, WritevScheduleWritableEvent) {
  std::string accumulator;
  schedulable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*schedulable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  io_handle_->initializeFileEvent(
      dispatcher_,
      [&should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          Buffer::OwnedImpl buf;
          Buffer::RawSlice slice;
          buf.reserve(1024, &slice, 1);
          auto result = handle->readv(1024, &slice, 1);
          if (result.ok()) {
            accumulator += absl::string_view(static_cast<char*>(slice.mem_), result.rc_);
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
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

TEST_F(UserSpaceIoSocketHandleTest, ReadAfterShutdownWrite) {
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
          Buffer::RawSlice slice;
          buf.reserve(1024, &slice, 1);
          auto result = handle->readv(1024, &slice, 1);
          if (result.ok()) {
            if (result.rc_ == 0) {
              should_close = true;
            } else {
              accumulator += absl::string_view(static_cast<char*>(slice.mem_), result.rc_);
            }
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
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

TEST_F(UserSpaceIoSocketHandleTest, NotifyWritableAfterShutdownWrite) {
  io_handle_peer_->setWatermarks(128);

  Buffer::OwnedImpl buf(std::string(256, 'a'));
  io_handle_->write(buf);
  EXPECT_FALSE(io_handle_peer_->isWritable());

  io_handle_->shutdown(ENVOY_SHUT_WR);
  FANCY_LOG(debug, "after {} shutdown write", static_cast<void*>(io_handle_.get()));

  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_peer_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read);
  EXPECT_CALL(cb_, called(Event::FileReadyType::Read));
  schedulable_cb->invokeCallback();
  EXPECT_FALSE(schedulable_cb->enabled_);

  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration()).Times(0);
  auto result = io_handle_peer_->recv(buf_.data(), buf_.size(), 0);
  EXPECT_EQ(256, result.rc_);
  // Readable event is not activated due to edge trigger type.
  EXPECT_FALSE(schedulable_cb->enabled_);

  // The `end of stream` is delivered.
  auto result_at_eof = io_handle_peer_->recv(buf_.data(), buf_.size(), 0);
  EXPECT_EQ(0, result_at_eof.rc_);

  // Also confirm `EOS` can triggered read ready event.
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, scheduleCallbackNextIteration());
  io_handle_peer_->enableFileEvents(Event::FileReadyType::Read);
  EXPECT_CALL(cb_, called(Event::FileReadyType::Read));
  schedulable_cb->invokeCallback();

  io_handle_peer_->close();
}

TEST_F(UserSpaceIoSocketHandleTest, NotSupportingMmsg) { EXPECT_FALSE(io_handle_->supportsMmsg()); }

TEST_F(UserSpaceIoSocketHandleTest, NotSupportsUdpGro) {
  EXPECT_FALSE(io_handle_->supportsUdpGro());
}

TEST_F(UserSpaceIoSocketHandleTest, DomainNullOpt) {
  EXPECT_FALSE(io_handle_->domain().has_value());
}

TEST_F(UserSpaceIoSocketHandleTest, Connect) {
  auto address_is_ignored =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_id");
  EXPECT_EQ(0, io_handle_->connect(address_is_ignored).rc_);
}

TEST_F(UserSpaceIoSocketHandleTest, ActivateEvent) {
  schedulable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  io_handle_->initializeFileEvent(
      dispatcher_, [&, handle = io_handle_.get()](uint32_t) {}, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read);
  EXPECT_FALSE(schedulable_cb_->enabled());
  io_handle_->activateFileEvents(Event::FileReadyType::Read);
  ASSERT_TRUE(schedulable_cb_->enabled());
}

TEST_F(UserSpaceIoSocketHandleTest, DeathOnActivatingDestroyedEvents) {
  io_handle_->resetFileEvents();
  ASSERT_DEBUG_DEATH(io_handle_->activateFileEvents(Event::FileReadyType::Read),
                     "Null user_file_event_");
}

TEST_F(UserSpaceIoSocketHandleTest, DeathOnEnablingDestroyedEvents) {
  io_handle_->resetFileEvents();
  ASSERT_DEBUG_DEATH(io_handle_->enableFileEvents(Event::FileReadyType::Read),
                     "Null user_file_event_");
}

TEST_F(UserSpaceIoSocketHandleTest, NotImplementDuplicate) {
  ASSERT_DEATH(io_handle_->duplicate(), "");
}

TEST_F(UserSpaceIoSocketHandleTest, NotImplementAccept) {
  ASSERT_DEATH(io_handle_->accept(nullptr, nullptr), "");
}

TEST_F(UserSpaceIoSocketHandleTest, LastRoundtripTimeNullOpt) {
  ASSERT_EQ(absl::nullopt, io_handle_->lastRoundTripTime());
}

class UserSpaceIoSocketHandleNotImplementedTest : public testing::Test {
public:
  UserSpaceIoSocketHandleNotImplementedTest() {
    io_handle_ = std::make_unique<UserSpaceIoSocketHandleImpl>();
    io_handle_peer_ = std::make_unique<UserSpaceIoSocketHandleImpl>();
    io_handle_->setPeerHandle(io_handle_peer_.get());
    io_handle_peer_->setPeerHandle(io_handle_.get());
  }

  ~UserSpaceIoSocketHandleNotImplementedTest() override {
    if (io_handle_->isOpen()) {
      io_handle_->close();
    }
    if (io_handle_peer_->isOpen()) {
      io_handle_peer_->close();
    }
  }

  std::unique_ptr<UserSpaceIoSocketHandleImpl> io_handle_;
  std::unique_ptr<UserSpaceIoSocketHandleImpl> io_handle_peer_;
  Buffer::RawSlice slice_;
};

TEST_F(UserSpaceIoSocketHandleNotImplementedTest, ErrorOnSetBlocking) {
  EXPECT_THAT(io_handle_->setBlocking(false), IsNotSupportedResult());
  EXPECT_THAT(io_handle_->setBlocking(true), IsNotSupportedResult());
}

TEST_F(UserSpaceIoSocketHandleNotImplementedTest, ErrorOnSendmsg) {
  EXPECT_THAT(io_handle_->sendmsg(&slice_, 0, 0, nullptr,
                                  Network::Address::EnvoyInternalInstance("listener_id")),
              IsInvalidAddress());
}

TEST_F(UserSpaceIoSocketHandleNotImplementedTest, ErrorOnRecvmsg) {
  Network::IoHandle::RecvMsgOutput output_is_ignored(1, nullptr);
  EXPECT_THAT(io_handle_->recvmsg(&slice_, 0, 0, output_is_ignored), IsInvalidAddress());
}

TEST_F(UserSpaceIoSocketHandleNotImplementedTest, ErrorOnRecvmmsg) {
  RawSliceArrays slices_is_ignored(1, absl::FixedArray<Buffer::RawSlice>({slice_}));
  Network::IoHandle::RecvMsgOutput output_is_ignored(1, nullptr);
  EXPECT_THAT(io_handle_->recvmmsg(slices_is_ignored, 0, output_is_ignored), IsInvalidAddress());
}

TEST_F(UserSpaceIoSocketHandleNotImplementedTest, ErrorOnBind) {
  auto address_is_ignored =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_id");
  EXPECT_THAT(io_handle_->bind(address_is_ignored), IsNotSupportedResult());
}

TEST_F(UserSpaceIoSocketHandleNotImplementedTest, ErrorOnListen) {
  int back_log_is_ignored = 0;
  EXPECT_THAT(io_handle_->listen(back_log_is_ignored), IsNotSupportedResult());
}

TEST_F(UserSpaceIoSocketHandleNotImplementedTest, ErrorOnAddress) {
  ASSERT_THROW(io_handle_->peerAddress(), EnvoyException);
  ASSERT_THROW(io_handle_->localAddress(), EnvoyException);
}

TEST_F(UserSpaceIoSocketHandleNotImplementedTest, ErrorOnSetOption) {
  EXPECT_THAT(io_handle_->setOption(0, 0, nullptr, 0), IsNotSupportedResult());
}

TEST_F(UserSpaceIoSocketHandleNotImplementedTest, ErrorOnGetOption) {
  EXPECT_THAT(io_handle_->getOption(0, 0, nullptr, nullptr), IsNotSupportedResult());
}
} // namespace
} // namespace UserSpaceIoSocket
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
