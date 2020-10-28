#include "envoy/common/platform.h"
#include "envoy/event/file_event.h"

#include "common/buffer/buffer_impl.h"
#include "common/network/address_impl.h"
#include "common/network/buffered_io_socket_handle_impl.h"

#include "test/mocks/event/mocks.h"

#include "absl/container/fixed_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;

namespace Envoy {
namespace Network {
namespace {

MATCHER(IsInvalidateAddress, "") {
  return arg.err_->getErrorCode() == Api::IoError::IoErrorCode::NoSupport;
}

MATCHER(IsNotSupportedResult, "") { return arg.errno_ == SOCKET_ERROR_NOT_SUP; }
class MockFileEventCallback {
public:
  MOCK_METHOD(void, called, (uint32_t arg));
};

class BufferedIoSocketHandleTest : public testing::Test {
public:
  BufferedIoSocketHandleTest() : buf_(1024) {
    io_handle_ = std::make_unique<Network::BufferedIoSocketHandleImpl>();
    io_handle_peer_ = std::make_unique<Network::BufferedIoSocketHandleImpl>();
    io_handle_->setWritablePeer(io_handle_peer_.get());
    io_handle_peer_->setWritablePeer(io_handle_.get());
  }

  ~BufferedIoSocketHandleTest() override {
    if (io_handle_->isOpen()) {
      io_handle_->close();
    }
    if (io_handle_peer_->isOpen()) {
      io_handle_peer_->close();
    }
  }

  void expectAgain() {
    auto result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
    EXPECT_FALSE(result.ok());
    EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  }

  NiceMock<Event::MockDispatcher> dispatcher_{};

  // Owned by BufferedIoSocketHandle.
  NiceMock<Event::MockSchedulableCallback>* scheduable_cb_;
  MockFileEventCallback cb_;
  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_;
  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_peer_;
  absl::FixedArray<char> buf_;
};

TEST_F(BufferedIoSocketHandleTest, TestErrorOnClosedIoHandle) {
  io_handle_->close();
  Api::IoCallUint64Result result{0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};
  result = io_handle_->recv(buf_.data(), buf_.size(), 0);
  ASSERT(!result.ok());
  ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());

  Buffer::OwnedImpl buf("0123456789");

  result = io_handle_->read(buf, 10);
  ASSERT(!result.ok());
  ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());

  Buffer::RawSlice slice;
  buf.reserve(1024, &slice, 1);
  result = io_handle_->readv(1024, &slice, 1);
  ASSERT(!result.ok());
  ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());

  result = io_handle_->write(buf);
  ASSERT(!result.ok());
  ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());

  result = io_handle_->writev(&slice, 1);
  ASSERT(!result.ok());
  ASSERT_EQ(Api::IoError::IoErrorCode::UnknownError, result.err_->getErrorCode());
}

// Test recv side effects.
TEST_F(BufferedIoSocketHandleTest, TestBasicRecv) {
  auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
  // EAGAIN.
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  io_handle_->setWriteEnd();
  result = io_handle_->recv(buf_.data(), buf_.size(), 0);
  EXPECT_TRUE(result.ok());
}

// Test recv side effects.
TEST_F(BufferedIoSocketHandleTest, TestReadEmpty) {
  Buffer::OwnedImpl buf;
  auto result = io_handle_->read(buf, 10);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  io_handle_->setWriteEnd();
  result = io_handle_->read(buf, 10);
  EXPECT_TRUE(result.ok());
}

// Test recv side effects.
TEST_F(BufferedIoSocketHandleTest, TestReadContent) {
  Buffer::OwnedImpl buf;
  auto& internal_buffer = io_handle_->getBufferForTest();
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

// Test recv side effects.
TEST_F(BufferedIoSocketHandleTest, TestBasicPeek) {
  auto result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
  // EAGAIN.
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, result.err_->getErrorCode());
  io_handle_->setWriteEnd();
  result = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
  EXPECT_TRUE(result.ok());
}

TEST_F(BufferedIoSocketHandleTest, TestRecvDrain) {
  auto& internal_buffer = io_handle_->getBufferForTest();
  internal_buffer.add("abcd");
  auto result = io_handle_->recv(buf_.data(), buf_.size(), 0);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(4, result.rc_);
  EXPECT_EQ(absl::string_view(buf_.data(), 4), "abcd");
  EXPECT_EQ(0, internal_buffer.length());
  expectAgain();
}

TEST_F(BufferedIoSocketHandleTest, FlowControl) {
  auto& internal_buffer = io_handle_->getBufferForTest();
  WritablePeer* handle_as_peer = io_handle_.get();
  internal_buffer.setWatermarks(128);
  EXPECT_FALSE(io_handle_->isReadable());
  EXPECT_TRUE(io_handle_peer_->isWritable());

  std::string big_chunk(256, 'a');
  internal_buffer.add(big_chunk);
  EXPECT_TRUE(io_handle_->isReadable());
  EXPECT_FALSE(handle_as_peer->isWritable());

  bool writable_flipped = false;
  // During the repeated recv, the writable flag must switch to true.
  while (internal_buffer.length() > 0) {
    SCOPED_TRACE(internal_buffer.length());
    EXPECT_TRUE(io_handle_->isReadable());
    bool writable = handle_as_peer->isWritable();
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
  EXPECT_TRUE(handle_as_peer->isWritable());
}

TEST_F(BufferedIoSocketHandleTest, EventScheduleBasic) {
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  EXPECT_CALL(cb_, called(_));
  scheduable_cb_->invokeCallback();
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestSetEnabledTriggerEventSchedule) {
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  ASSERT_TRUE(scheduable_cb_->enabled());
  EXPECT_CALL(cb_, called(Event::FileReadyType::Read));
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(scheduable_cb_->enabled());

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->enableFileEvents(Event::FileReadyType::Read);
  ASSERT_TRUE(scheduable_cb_->enabled());
  EXPECT_CALL(cb_, called(_));
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(scheduable_cb_->enabled());

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->enableFileEvents(Event::FileReadyType::Write);
  ASSERT_TRUE(scheduable_cb_->enabled());
  EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(scheduable_cb_->enabled());

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->enableFileEvents(Event::FileReadyType::Write | Event::FileReadyType::Read);
  ASSERT_TRUE(scheduable_cb_->enabled());
  EXPECT_CALL(cb_, called(Event::FileReadyType::Write | Event::FileReadyType::Read));
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(scheduable_cb_->enabled());
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestReadAndWriteAreEdgeTriggered) {
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  EXPECT_CALL(cb_, called(_));
  scheduable_cb_->invokeCallback();

  // Neither read and write will trigger self readiness.
  EXPECT_CALL(cb_, called(_)).Times(0);

  // Drain 1 bytes.
  auto& internal_buffer = io_handle_->getBufferForTest();
  internal_buffer.add("abcd");
  auto result = io_handle_->recv(buf_.data(), 1, 0);
  EXPECT_TRUE(result.ok());
  EXPECT_EQ(1, result.rc_);

  ASSERT_FALSE(scheduable_cb_->enabled());
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestSetDisabledBlockEventSchedule) {
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  io_handle_->enableFileEvents(0);

  EXPECT_CALL(cb_, called(0));
  scheduable_cb_->invokeCallback();

  ASSERT_FALSE(scheduable_cb_->enabled());
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestEventResetClearCallback) {
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  ASSERT_TRUE(scheduable_cb_->enabled());
  
  EXPECT_CALL(cb_, called(_)).Times(0);
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestDrainToLowWaterMarkTriggerReadEvent) {
  auto& internal_buffer = io_handle_->getBufferForTest();
  WritablePeer* handle_as_peer = io_handle_.get();
  internal_buffer.setWatermarks(128);
  EXPECT_FALSE(io_handle_->isReadable());
  EXPECT_TRUE(io_handle_peer_->isWritable());

  std::string big_chunk(256, 'a');
  internal_buffer.add(big_chunk);
  EXPECT_TRUE(io_handle_->isReadable());
  EXPECT_FALSE(handle_as_peer->isWritable());

  // Clear invoke callback on peer.
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  ASSERT_TRUE(scheduable_cb_->enabled());
  EXPECT_CALL(cb_, called(_));
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(scheduable_cb_->enabled());

  {
    auto result = io_handle_->recv(buf_.data(), 1, 0);
    EXPECT_FALSE(handle_as_peer->isWritable());
  }
  {
    EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration()).Times(1);
    auto result = io_handle_->recv(buf_.data(), 232, 0);
    EXPECT_TRUE(handle_as_peer->isWritable());
    EXPECT_CALL(cb_, called(_));
    scheduable_cb_->invokeCallback();
  }

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration()).Times(1);
  io_handle_->close();
}

TEST_F(BufferedIoSocketHandleTest, TestClose) {
  auto& internal_buffer = io_handle_->getBufferForTest();
  internal_buffer.add("abcd");
  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
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
        if (events & Event::FileReadyType::Write) {
          Buffer::OwnedImpl buf("");
          auto result = io_handle_->write(buf);
          if (!result.ok() && result.err_->getErrorCode() != Api::IoError::IoErrorCode::Again) {
            should_close = true;
          }
        }
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read | Event::FileReadyType::Write);
  scheduable_cb_->invokeCallback();

  // Not closed yet.
  ASSERT_FALSE(should_close);

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->close();

  ASSERT_TRUE(scheduable_cb_->enabled());
  scheduable_cb_->invokeCallback();
  ASSERT_TRUE(should_close);

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration()).Times(0);
  io_handle_->close();
  EXPECT_EQ(4, accumulator.size());
  io_handle_->resetFileEvents();
}

// Test that a readable event is raised when peer shutdown write. Also confirm read will return
// EAGAIN.
TEST_F(BufferedIoSocketHandleTest, TestShutDownRaiseEvent) {
  auto& internal_buffer = io_handle_->getBufferForTest();
  internal_buffer.add("abcd");

  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
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
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  scheduable_cb_->invokeCallback();

  // Not closed yet.
  ASSERT_FALSE(should_close);

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->shutdown(ENVOY_SHUT_WR);

  ASSERT_TRUE(scheduable_cb_->enabled());
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(should_close);
  EXPECT_EQ(4, accumulator.size());
  io_handle_->close();
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestRepeatedShutdownWR) {
  EXPECT_EQ(io_handle_peer_->shutdown(ENVOY_SHUT_WR).rc_, 0);
  ENVOY_LOG_MISC(debug, "lambdai: next shutdown");
  EXPECT_EQ(io_handle_peer_->shutdown(ENVOY_SHUT_WR).rc_, 0);
}

TEST_F(BufferedIoSocketHandleTest, TestShutDownOptionsNotSupported) {
  ASSERT_DEBUG_DEATH(io_handle_peer_->shutdown(ENVOY_SHUT_RD), "");
  ASSERT_DEBUG_DEATH(io_handle_peer_->shutdown(ENVOY_SHUT_RDWR), "");
}

TEST_F(BufferedIoSocketHandleTest, TestWriteByMove) {
  Buffer::OwnedImpl buf("0123456789");
  io_handle_peer_->write(buf);
  auto& internal_buffer = io_handle_->getBufferForTest();
  EXPECT_EQ("0123456789", internal_buffer.toString());
  EXPECT_EQ(0, buf.length());
}

// Test write return error code. Ignoring the side effect of event scheduling.
TEST_F(BufferedIoSocketHandleTest, TestWriteErrorCode) {
  Buffer::OwnedImpl buf("0123456789");
  Api::IoCallUint64Result result{0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};

  {
    // Populate write destination with massive data so as to not writable.
    auto& internal_buffer = io_handle_peer_->getBufferForTest();
    internal_buffer.setWatermarks(1024);
    internal_buffer.add(std::string(2048, ' '));

    result = io_handle_->write(buf);
    ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
    EXPECT_EQ(10, buf.length());
  }

  {
    // Write after shutdown.
    io_handle_->shutdown(ENVOY_SHUT_WR);
    result = io_handle_->write(buf);
    ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
    EXPECT_EQ(10, buf.length());
  }

  {
    io_handle_peer_->close();
    EXPECT_TRUE(io_handle_->isOpen());
    result = io_handle_->write(buf);
    ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
  }
}

// Test writev return error code. Ignoring the side effect of event scheduling.
TEST_F(BufferedIoSocketHandleTest, TestWritevErrorCode) {
  std::string buf(10, 'a');
  Buffer::RawSlice slice{static_cast<void*>(buf.data()), 10};
  Api::IoCallUint64Result result{0, Api::IoErrorPtr(nullptr, [](Api::IoError*) {})};

  {
    // Populate write destination with massive data so as to not writable.
    auto& internal_buffer = io_handle_peer_->getBufferForTest();
    internal_buffer.setWatermarks(1024);
    internal_buffer.add(std::string(2048, ' '));
    result = io_handle_->writev(&slice, 1);
    ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::Again);
  }

  {
    // Writev after shutdown.
    io_handle_->shutdown(ENVOY_SHUT_WR);
    result = io_handle_->writev(&slice, 1);
    ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
  }

  {
    // Close the peer.
    io_handle_peer_->close();
    EXPECT_TRUE(io_handle_->isOpen());
    result = io_handle_->writev(&slice, 1);
    ASSERT_EQ(result.err_->getErrorCode(), Api::IoError::IoErrorCode::UnknownError);
  }
}

TEST_F(BufferedIoSocketHandleTest, TestWritevToPeer) {
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
  auto& internal_buffer = io_handle_->getBufferForTest();
  EXPECT_EQ(3, internal_buffer.length());
  EXPECT_EQ("012", internal_buffer.toString());
}

TEST_F(BufferedIoSocketHandleTest, TestWriteScheduleWritableEvent) {
  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
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
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  scheduable_cb_->invokeCallback();
  EXPECT_FALSE(scheduable_cb_->enabled());

  Buffer::OwnedImpl data_to_write("0123456789");
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->write(data_to_write);
  EXPECT_EQ(0, data_to_write.length());

  EXPECT_TRUE(scheduable_cb_->enabled());
  scheduable_cb_->invokeCallback();
  EXPECT_EQ("0123456789", accumulator);
  EXPECT_FALSE(should_close);

  io_handle_->close();
}

TEST_F(BufferedIoSocketHandleTest, TestWritevScheduleWritableEvent) {
  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
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
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  scheduable_cb_->invokeCallback();
  EXPECT_FALSE(scheduable_cb_->enabled());

  std::string raw_data("0123456789");
  Buffer::RawSlice slice{static_cast<void*>(raw_data.data()), raw_data.size()};
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_peer_->writev(&slice, 1);

  EXPECT_TRUE(scheduable_cb_->enabled());
  scheduable_cb_->invokeCallback();
  EXPECT_EQ("0123456789", accumulator);
  EXPECT_FALSE(should_close);

  io_handle_->close();
}

TEST_F(BufferedIoSocketHandleTest, TestReadAfterShutdownWrite) {
  io_handle_peer_->shutdown(ENVOY_SHUT_WR);
  ENVOY_LOG_MISC(debug, "after {} shutdown write ", static_cast<void*>(io_handle_peer_.get()));
  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
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
            accumulator += absl::string_view(static_cast<char*>(slice.mem_), result.rc_);
          } else if (result.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "read returns EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "will close");
            should_close = true;
          }
        }
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  scheduable_cb_->invokeCallback();

  EXPECT_FALSE(scheduable_cb_->enabled());
  std::string raw_data("0123456789");
  Buffer::RawSlice slice{static_cast<void*>(raw_data.data()), raw_data.size()};
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->writev(&slice, 1);
  EXPECT_TRUE(scheduable_cb_->enabled());

  scheduable_cb_->invokeCallback();
  EXPECT_FALSE(scheduable_cb_->enabled());
  EXPECT_EQ(raw_data, accumulator);

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->close();
  io_handle_->resetFileEvents();
}

TEST_F(BufferedIoSocketHandleTest, TestNotififyWritableAfterShutdownWrite) {
  auto& peer_internal_buffer = io_handle_peer_->getBufferForTest();
  peer_internal_buffer.setWatermarks(128);
  std::string big_chunk(256, 'a');
  peer_internal_buffer.add(big_chunk);
  EXPECT_FALSE(io_handle_peer_->isWritable());

  io_handle_peer_->shutdown(ENVOY_SHUT_WR);
  ENVOY_LOG_MISC(debug, "after {} shutdown write", static_cast<void*>(io_handle_peer_.get()));

  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [&, handle = io_handle_.get()](uint32_t) {}, Event::PlatformDefaultTriggerType,
      Event::FileReadyType::Read);
  scheduable_cb_->invokeCallback();
  EXPECT_FALSE(scheduable_cb_->enabled());

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  peer_internal_buffer.drain(peer_internal_buffer.length());
  EXPECT_TRUE(scheduable_cb_->enabled());

  io_handle_->close();
}

TEST_F(BufferedIoSocketHandleTest, TestNotSupportingMmsg) {
  EXPECT_FALSE(io_handle_->supportsMmsg());
}

TEST_F(BufferedIoSocketHandleTest, TestNotSupportsUdpGro) {
  EXPECT_FALSE(io_handle_->supportsUdpGro());
}

TEST_F(BufferedIoSocketHandleTest, TestDomainNullOpt) {
  EXPECT_FALSE(io_handle_->domain().has_value());
}

TEST_F(BufferedIoSocketHandleTest, TestConnect) {
  auto address_is_ignored =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_id");
  EXPECT_EQ(0, io_handle_->connect(address_is_ignored).rc_);
}

class BufferedIoSocketHandleNotImplementedTest : public testing::Test {
public:
  BufferedIoSocketHandleNotImplementedTest() {
    io_handle_ = std::make_unique<Network::BufferedIoSocketHandleImpl>();
    io_handle_peer_ = std::make_unique<Network::BufferedIoSocketHandleImpl>();
    io_handle_->setWritablePeer(io_handle_peer_.get());
    io_handle_peer_->setWritablePeer(io_handle_.get());
  }

  ~BufferedIoSocketHandleNotImplementedTest() override {
    if (io_handle_->isOpen()) {
      io_handle_->close();
    }
    if (io_handle_peer_->isOpen()) {
      io_handle_peer_->close();
    }
  }

  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_;
  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_peer_;
  Buffer::RawSlice slice_;
};

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnSetBlocking) {
  EXPECT_THAT(io_handle_->setBlocking(false), IsNotSupportedResult());
  EXPECT_THAT(io_handle_->setBlocking(true), IsNotSupportedResult());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnSendmsg) {
  EXPECT_THAT(io_handle_->sendmsg(&slice_, 0, 0, nullptr,
                                  Network::Address::EnvoyInternalInstance("listener_id")),
              IsInvalidateAddress());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnRecvmsg) {
  Network::IoHandle::RecvMsgOutput output_is_ignored(1, nullptr);
  EXPECT_THAT(io_handle_->recvmsg(&slice_, 0, 0, output_is_ignored), IsInvalidateAddress());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnRecvmmsg) {
  RawSliceArrays slices_is_ignored(1, absl::FixedArray<Buffer::RawSlice>({slice_}));
  Network::IoHandle::RecvMsgOutput output_is_ignored(1, nullptr);
  EXPECT_THAT(io_handle_->recvmmsg(slices_is_ignored, 0, output_is_ignored), IsInvalidateAddress());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnBind) {
  auto address_is_ignored =
      std::make_shared<Network::Address::EnvoyInternalInstance>("listener_id");
  EXPECT_THAT(io_handle_->bind(address_is_ignored), IsNotSupportedResult());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnListen) {
  int back_log_is_ignored = 0;
  EXPECT_THAT(io_handle_->listen(back_log_is_ignored), IsNotSupportedResult());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnAddress) {
  ASSERT_THROW(io_handle_->peerAddress(), EnvoyException);
  ASSERT_THROW(io_handle_->localAddress(), EnvoyException);
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnSetOption) {
  EXPECT_THAT(io_handle_->setOption(0, 0, nullptr, 0), IsNotSupportedResult());
}

TEST_F(BufferedIoSocketHandleNotImplementedTest, TestErrorOnGetOption) {
  EXPECT_THAT(io_handle_->getOption(0, 0, nullptr, nullptr), IsNotSupportedResult());
}
} // namespace
} // namespace Network
} // namespace Envoy
