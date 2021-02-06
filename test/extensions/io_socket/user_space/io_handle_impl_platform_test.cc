#include "envoy/common/platform.h"
#include "envoy/event/file_event.h"

#include "extensions/io_socket/user_space/io_handle_impl.h"

#include "test/mocks/event/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {
namespace {

using testing::NiceMock;

class MockFileEventCallback {
public:
  MOCK_METHOD(void, called, (uint32_t arg));
};

class IoHandleImplPlatformTest : public testing::Test {
public:
  IoHandleImplPlatformTest() {
    first_io_handle_ = std::make_unique<IoHandleImpl>();
    second_io_handle_ = std::make_unique<IoHandleImpl>();
    first_io_handle_->setPeerHandle(second_io_handle_.get());
    second_io_handle_->setPeerHandle(first_io_handle_.get());
  }

  ~IoHandleImplPlatformTest() override {
    if (first_io_handle_->isOpen()) {
      first_io_handle_->close();
    }
    if (second_io_handle_->isOpen()) {
      second_io_handle_->close();
    }
  }

  std::unique_ptr<IoHandleImpl> first_io_handle_;
  std::unique_ptr<IoHandleImpl> second_io_handle_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  MockFileEventCallback cb_;
};

TEST_F(IoHandleImplPlatformTest, CreatePlatformDefaultTriggerTypeFailOnWindows) {
  // schedulable_cb will be destroyed by IoHandle.
  auto schedulable_cb = new Event::MockSchedulableCallback(&dispatcher_);
  EXPECT_CALL(*schedulable_cb, enabled());
  EXPECT_CALL(*schedulable_cb, cancel());
  first_io_handle_->initializeFileEvent(
      dispatcher_, [this](uint32_t events) { cb_.called(events); },
      Event::PlatformDefaultTriggerType, Event::FileReadyType::Read);
}

} // namespace
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
