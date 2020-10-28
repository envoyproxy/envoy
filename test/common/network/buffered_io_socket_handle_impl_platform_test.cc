#include "envoy/common/platform.h"
#include "envoy/event/file_event.h"

#include "common/network/buffered_io_socket_handle_impl.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Network {
namespace {

using testing::NiceMock;

class MockFileEventCallback {
public:
  MOCK_METHOD(void, called, (uint32_t arg));
};

// Explicitly mark the test failing on windows and will be fixed.
class BufferedIoSocketHandlePlatformTest : public testing::Test {
public:
  BufferedIoSocketHandlePlatformTest() {
    io_handle_ = std::make_unique<Network::BufferedIoSocketHandleImpl>();
    io_handle_peer_ = std::make_unique<Network::BufferedIoSocketHandleImpl>();
    io_handle_->setWritablePeer(io_handle_peer_.get());
    io_handle_peer_->setWritablePeer(io_handle_.get());
  }

  ~BufferedIoSocketHandlePlatformTest() override {
    if (io_handle_->isOpen()) {
      io_handle_->close();
    }
    if (io_handle_peer_->isOpen()) {
      io_handle_peer_->close();
    }
  }

  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_;
  std::unique_ptr<Network::BufferedIoSocketHandleImpl> io_handle_peer_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  MockFileEventCallback cb_;
};

TEST_F(BufferedIoSocketHandlePlatformTest, TestCreatePlatformDefaultTriggerTypeFailOnWindows) {
  auto scheduable_cb = new NiceMock<Event::MockSchedulableCallback>(&dispatcher_);
  EXPECT_CALL(*scheduable_cb, scheduleCallbackNextIteration());
  io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
}

} // namespace
} // namespace Network
} // namespace Envoy
