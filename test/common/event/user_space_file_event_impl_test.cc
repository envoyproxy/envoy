#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/user_space_file_event_impl.h"
#include "common/network/peer_buffer.h"

#include "test/mocks/common.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace {

using testing::NiceMock;
using testing::Return;

constexpr auto event_rw = Event::FileReadyType::Read | Event::FileReadyType::Write;
class MockReadyCb {
public:
  MOCK_METHOD(void, called, (uint32_t));
};

class MockReadWritable : public Network::ReadWritable {
public:
  MOCK_METHOD(void, setWriteEnd, ());
  MOCK_METHOD(bool, isWriteEndSet, ());
  MOCK_METHOD(void, onPeerDestroy, ());
  MOCK_METHOD(void, maybeSetNewData, ());
  MOCK_METHOD(Buffer::Instance*, getWriteBuffer, ());
  MOCK_METHOD(bool, isWritable, (), (const));
  MOCK_METHOD(bool, isPeerWritable, (), (const));
  MOCK_METHOD(void, onPeerBufferWritable, ());
  MOCK_METHOD(bool, isPeerShutDownWrite, (), (const));
  MOCK_METHOD(bool, isOverHighWatermark, (), (const));
  MOCK_METHOD(bool, isReadable, (), (const));
};
class UserSpaceFileEventImplTest : public testing::Test {
public:
  UserSpaceFileEventImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void setWritable() { EXPECT_CALL(io_source_, isPeerWritable()).WillRepeatedly(Return(true)); }
  void setReadable() { EXPECT_CALL(io_source_, isReadable()).WillRepeatedly(Return(true)); }

protected:
  NiceMock<MockReadWritable> io_source_;
  MockReadyCb ready_cb_;
  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
  std::unique_ptr<Event::UserSpaceFileEventImpl> user_file_event_;
};

TEST_F(UserSpaceFileEventImplTest, TestEnabledEventsTriggeredAfterCreate) {
  setWritable();
  user_file_event_ = std::make_unique<Event::UserSpaceFileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
  EXPECT_CALL(ready_cb_, called(FileReadyType::Write));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(UserSpaceFileEventImplTest, TestRescheduleAfterTriggered) {
  user_file_event_ = std::make_unique<Event::UserSpaceFileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
  {
    SCOPED_TRACE("1st schedule");
    user_file_event_->activate(event_rw);
    EXPECT_CALL(ready_cb_, called(event_rw));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  {
    SCOPED_TRACE("2nd schedule");
    user_file_event_->activate(event_rw);
    EXPECT_CALL(ready_cb_, called(event_rw));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(UserSpaceFileEventImplTest, TestRescheduleIsDeduplicated) {
  user_file_event_ = std::make_unique<Event::UserSpaceFileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
  {
    SCOPED_TRACE("1st schedule");
    user_file_event_->activate(event_rw);

    user_file_event_->activate(event_rw);
    EXPECT_CALL(ready_cb_, called(event_rw)).Times(1);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  {
    SCOPED_TRACE("further dispatcher drive");
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(UserSpaceFileEventImplTest, TestDefaultReturnAllEnabledReadAndWriteEvents) {
  {
    auto current_event = Event::FileReadyType::Read;
    SCOPED_TRACE(absl::StrCat("current event:", current_event));
    EXPECT_CALL(io_source_, isReadable()).WillOnce(Return(true)).RetiresOnSaturation();
    EXPECT_CALL(io_source_, isPeerWritable()).WillOnce(Return(false)).RetiresOnSaturation();
    user_file_event_ = std::make_unique<Event::UserSpaceFileEventImpl>(
        *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
    // user_file_event_->activate(e);
    EXPECT_CALL(ready_cb_, called(current_event));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    user_file_event_.reset();
  }

  {
    auto current_event = Event::FileReadyType::Write;
    SCOPED_TRACE(absl::StrCat("current event:", current_event));
    EXPECT_CALL(io_source_, isReadable()).WillOnce(Return(false)).RetiresOnSaturation();
    EXPECT_CALL(io_source_, isPeerWritable()).WillOnce(Return(true)).RetiresOnSaturation();
    user_file_event_ = std::make_unique<Event::UserSpaceFileEventImpl>(
        *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
    // user_file_event_->activate(e);
    EXPECT_CALL(ready_cb_, called(current_event));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    user_file_event_.reset();
  }

  {
    auto current_event = event_rw;
    SCOPED_TRACE(absl::StrCat("current event:", current_event));
    EXPECT_CALL(io_source_, isReadable()).WillOnce(Return(true)).RetiresOnSaturation();
    EXPECT_CALL(io_source_, isPeerWritable()).WillOnce(Return(true)).RetiresOnSaturation();
    user_file_event_ = std::make_unique<Event::UserSpaceFileEventImpl>(
        *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
    // user_file_event_->activate(e);
    EXPECT_CALL(ready_cb_, called(current_event));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    user_file_event_.reset();
  }
}

TEST_F(UserSpaceFileEventImplTest, TestActivateWillSchedule) {
  // IO is neither readable nor writable.
  user_file_event_ = std::make_unique<Event::UserSpaceFileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activate(Event::FileReadyType::Read);
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Read)).Times(1);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activate(Event::FileReadyType::Write);
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Write)).Times(1);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(UserSpaceFileEventImplTest, TestActivateDedup) {
  // IO is neither readable nor writable.
  user_file_event_ = std::make_unique<Event::UserSpaceFileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activate(Event::FileReadyType::Read);
    user_file_event_->activate(Event::FileReadyType::Write);
    user_file_event_->activate(Event::FileReadyType::Write);
    user_file_event_->activate(Event::FileReadyType::Read);
    EXPECT_CALL(ready_cb_, called(event_rw)).Times(1);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(UserSpaceFileEventImplTest, TestEnabledClearActivate) {
  // IO is neither readable nor writable.
  user_file_event_ = std::make_unique<Event::UserSpaceFileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  {
    setWritable();
    setReadable();
    user_file_event_->activate(Event::FileReadyType::Read);
    user_file_event_->setEnabled(Event::FileReadyType::Write);
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Write)).Times(1);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(UserSpaceFileEventImplTest, TestEventClosedIsNotTriggeredUnlessManullyActivated) {
  user_file_event_ = std::make_unique<Event::UserSpaceFileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); },
      Event::FileReadyType::Write | Event::FileReadyType::Closed, io_source_);
  {
    // No Closed event bit if enabled by not activated.
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    setWritable();
    setReadable();
    user_file_event_->activate(Event::FileReadyType::Closed);
    // Activate could deliver Closed event bit.
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Closed))
        .Times(1);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

} // namespace
} // namespace Event
} // namespace Envoy