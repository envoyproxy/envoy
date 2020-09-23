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
    io_handle_->close();
    io_handle_peer_->close();
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

TEST_F(BufferedIoSocketHandleTest, TestClose) {
}

TEST_F(BufferedIoSocketHandleTest, TestShutdown) {
}

} // namespace
} // namespace Network
} // namespace Envoy