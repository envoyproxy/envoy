#include <sys/socket.h>

#include "common/network/buffered_io_socket_handle_impl.h"

#include "envoy/event/file_event.h"
#include "test/mocks/event/mocks.h"

#include "absl/container/fixed_array.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::InSequence;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::WithArgs;

namespace Envoy {
namespace Network {
namespace {

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
    auto res = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
    EXPECT_FALSE(res.ok());
    EXPECT_EQ(Api::IoError::IoErrorCode::Again, res.err_->getErrorCode());
  }
  NiceMock<Event::MockDispatcher> dispatcher_{};

  // Owned by BufferedIoSocketHandle.
  NiceMock<Event::MockSchedulableCallback>* scheduable_cb_;
  MockFileEventCallback cb_;
  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_;
  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_peer_;
  absl::FixedArray<char> buf_;
};

// Test recv side effects.
TEST_F(BufferedIoSocketHandleTest, TestBasicRecv) {
  auto res = io_handle_->recv(buf_.data(), buf_.size(), 0);
  // EAGAIN.
  EXPECT_FALSE(res.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, res.err_->getErrorCode());
  io_handle_->setWriteEnd();
  res = io_handle_->recv(buf_.data(), buf_.size(), 0);
  EXPECT_FALSE(res.ok());
  EXPECT_NE(Api::IoError::IoErrorCode::Again, res.err_->getErrorCode());
}

// Test recv side effects.
TEST_F(BufferedIoSocketHandleTest, TestBasicPeek) {
  auto res = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
  // EAGAIN.
  EXPECT_FALSE(res.ok());
  EXPECT_EQ(Api::IoError::IoErrorCode::Again, res.err_->getErrorCode());
  io_handle_->setWriteEnd();
  res = io_handle_->recv(buf_.data(), buf_.size(), MSG_PEEK);
  EXPECT_FALSE(res.ok());
  EXPECT_NE(Api::IoError::IoErrorCode::Again, res.err_->getErrorCode());
}

TEST_F(BufferedIoSocketHandleTest, TestRecvDrain) {
  auto& internal_buffer = io_handle_->getBufferForTest();
  internal_buffer.add("abcd");
  auto res = io_handle_->recv(buf_.data(), buf_.size(), 0);
  EXPECT_TRUE(res.ok());
  EXPECT_EQ(4, res.rc_);
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
    auto res = io_handle_->recv(buf_.data(), 32, 0);
    EXPECT_TRUE(res.ok());
    EXPECT_EQ(32, res.rc_);
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
  auto ev = io_handle_->createFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  EXPECT_CALL(cb_, called(_));
  scheduable_cb_->invokeCallback();
  ev.reset();
}

TEST_F(BufferedIoSocketHandleTest, TestSetEnabledTriggerEventSchedule) {
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  auto ev = io_handle_->createFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  ASSERT_TRUE(scheduable_cb_->enabled());
  EXPECT_CALL(cb_, called(Event::FileReadyType::Read));
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(scheduable_cb_->enabled());

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  ev->setEnabled(Event::FileReadyType::Read);
  ASSERT_TRUE(scheduable_cb_->enabled());
  EXPECT_CALL(cb_, called(_));
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(scheduable_cb_->enabled());

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  ev->setEnabled(Event::FileReadyType::Write);
  ASSERT_TRUE(scheduable_cb_->enabled());
  EXPECT_CALL(cb_, called(Event::FileReadyType::Write));
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(scheduable_cb_->enabled());

  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  ev->setEnabled(Event::FileReadyType::Write | Event::FileReadyType::Read);
  ASSERT_TRUE(scheduable_cb_->enabled());
  EXPECT_CALL(cb_, called(Event::FileReadyType::Write | Event::FileReadyType::Read));
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(scheduable_cb_->enabled());
  ev.reset();
}

TEST_F(BufferedIoSocketHandleTest, TestReadAndWriteAreEdgeTriggered) {
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  auto ev = io_handle_->createFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  EXPECT_CALL(cb_, called(_));
  scheduable_cb_->invokeCallback();

  // Neither read and write will trigger self readiness.
  EXPECT_CALL(cb_, called(_)).Times(0);

  // Drain 1 bytes.
  auto& internal_buffer = io_handle_->getBufferForTest();
  internal_buffer.add("abcd");
  auto res = io_handle_->recv(buf_.data(), 1, 0);
  EXPECT_TRUE(res.ok());
  EXPECT_EQ(1, res.rc_);

  ASSERT_FALSE(scheduable_cb_->enabled());
  ev.reset();
}

TEST_F(BufferedIoSocketHandleTest, TestSetDisabledBlockEventSchedule) {
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  auto ev = io_handle_->createFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);

  ev->setEnabled(0);

  EXPECT_CALL(cb_, called(0));
  scheduable_cb_->invokeCallback();

  ASSERT_FALSE(scheduable_cb_->enabled());
  ev.reset();
}

TEST_F(BufferedIoSocketHandleTest, TestEventResetClearCallback) {
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  auto ev = io_handle_->createFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  ASSERT_TRUE(scheduable_cb_->enabled());

  ev.reset();
  ASSERT_FALSE(scheduable_cb_->enabled());
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
  auto ev = io_handle_peer_->createFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  ASSERT_TRUE(scheduable_cb_->enabled());
  EXPECT_CALL(cb_, called(_));
  scheduable_cb_->invokeCallback();
  ASSERT_FALSE(scheduable_cb_->enabled());

  {
    auto res = io_handle_->recv(buf_.data(), 1, 0);
    EXPECT_FALSE(handle_as_peer->isWritable());
  }
  {
    EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration()).Times(1);
    auto res = io_handle_->recv(buf_.data(), 232, 0);
    EXPECT_TRUE(handle_as_peer->isWritable());
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
  auto ev = io_handle_->createFileEvent(
      dispatcher_,
      [this, &should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          auto res = io_handle_->recv(buf_.data(), buf_.size(), 0);
          if (res.ok()) {
            accumulator += absl::string_view(buf_.data(), res.rc_);
          } else if (res.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "lambdai: EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "lambdai: close, not schedule event");
            should_close = true;
          }
        }
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
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
  ev.reset();
}

TEST_F(BufferedIoSocketHandleTest, TestShutdown) {
  auto& internal_buffer = io_handle_->getBufferForTest();
  internal_buffer.add("abcd");

  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  auto ev = io_handle_->createFileEvent(
      dispatcher_,
      [this, &should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          auto res = io_handle_->recv(buf_.data(), buf_.size(), 0);
          if (res.ok()) {
            accumulator += absl::string_view(buf_.data(), res.rc_);
          } else if (res.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "lambdai: EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "lambdai: close, not schedule event");
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
  ASSERT_TRUE(should_close);
  EXPECT_EQ(4, accumulator.size());
  io_handle_->close();
  ev.reset();
}

TEST_F(BufferedIoSocketHandleTest, TestWriteToPeer) {
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
  auto ev = io_handle_->createFileEvent(
      dispatcher_,
      [this, &should_close, handle = io_handle_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          auto& internal_buffer = handle->getBufferForTest();
          Buffer::RawSlice slice;
          internal_buffer.reserve(1024, &slice, 1);
          auto res = io_handle_->readv(1024, &slice, 1);
          if (res.ok()) {
            accumulator += absl::string_view(static_cast<char*>(slice.mem_), res.rc_);
          } else if (res.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "lambdai: EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "lambdai: close, not schedule event");
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

TEST_F(BufferedIoSocketHandleTest, TestReadFlowAfterShutdownWrite) {

  io_handle_peer_->shutdown(ENVOY_SHUT_WR);

  std::string accumulator;
  scheduable_cb_ = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb_, scheduleCallbackNextIteration());
  bool should_close = false;
  auto ev = io_handle_peer_->createFileEvent(
      dispatcher_,
      [this, &should_close, handle = io_handle_peer_.get(), &accumulator](uint32_t events) {
        if (events & Event::FileReadyType::Read) {
          auto res = io_handle_peer_->recv(buf_.data(), buf_.size(), 0);
          if (res.ok()) {
            accumulator += absl::string_view(buf_.data(), res.rc_);
          } else if (res.err_->getErrorCode() == Api::IoError::IoErrorCode::Again) {
            ENVOY_LOG_MISC(debug, "lambdai: EAGAIN");
          } else {
            ENVOY_LOG_MISC(debug, "lambdai: close, not schedule event");
            should_close = true;
          }
        }
      },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
  scheduable_cb_->invokeCallback();

  EXPECT_FALSE(scheduable_cb_->enabled());
  std::string raw_data("0123456789");
  Buffer::RawSlice slice{static_cast<void*>(raw_data.data()), raw_data.size()};
  io_handle_->writev(&slice, 1);
  EXPECT_TRUE(scheduable_cb_->enabled());

  scheduable_cb_->invokeCallback();
  EXPECT_FALSE(scheduable_cb_->enabled());
  EXPECT_EQ(raw_data, accumulator);

  ev.reset();
}

} // namespace
} // namespace Network
} // namespace Envoy