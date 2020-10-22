#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/event/dispatcher_impl.h"
#include "common/event/user_space_file_event_impl.h"

#include "test/mocks/common.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Event {
namespace {

constexpr auto event_rw = Event::FileReadyType::Read | Event::FileReadyType::Write;
class MockReadyCb {
public:
  MOCK_METHOD(void, called, (uint32_t));
};

class UserSpaceFileEventImplTest : public testing::Test {
public:
  UserSpaceFileEventImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {
    io_callback_ =
        dispatcher_->createSchedulableCallback([this]() { user_file_event_->onEvents(); });
  }

  void scheduleNextEvent() {
    ASSERT(io_callback_ != nullptr);
    io_callback_->scheduleCallbackNextIteration();
  }

protected:
  MockReadyCb ready_cb_;
  Api::ApiPtr api_;
  DispatcherPtr dispatcher_;
  Event::SchedulableCallbackPtr io_callback_;
  std::unique_ptr<Event::UserSpaceFileEventImpl> user_file_event_;
};

TEST_F(UserSpaceFileEventImplTest, TestLevelTriggerIsNotSupported) {
  ASSERT_DEBUG_DEATH(Event::UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
                         *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); },
                         Event::FileTriggerType::Level, event_rw, *io_callback_),
                     "assert failure");
}

TEST_F(UserSpaceFileEventImplTest, TestEnabledEventsTriggeredAfterCreate) {
  user_file_event_ = Event::UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, Event::FileTriggerType::Edge,
      event_rw, *io_callback_);
  scheduleNextEvent();
  EXPECT_CALL(ready_cb_, called(event_rw));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(UserSpaceFileEventImplTest, TestDebugDeathOnActivateDisabledEvents) {
  user_file_event_ = Event::UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Read, *io_callback_);
  ASSERT_DEBUG_DEATH(user_file_event_->activate(Event::FileReadyType::Write), "");
}

TEST_F(UserSpaceFileEventImplTest, TestRescheduleAfterTriggered) {
  user_file_event_ = Event::UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, Event::FileTriggerType::Edge,
      event_rw, *io_callback_);
  {
    SCOPED_TRACE("1st schedule");
    scheduleNextEvent();
    EXPECT_CALL(ready_cb_, called(event_rw));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  {
    SCOPED_TRACE("2nd schedule");
    scheduleNextEvent();
    EXPECT_CALL(ready_cb_, called(event_rw));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(UserSpaceFileEventImplTest, TestRescheduleIsDeduplicated) {
  user_file_event_ = Event::UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, Event::FileTriggerType::Edge,
      event_rw, *io_callback_);
  {
    SCOPED_TRACE("1st schedule");
    scheduleNextEvent();
    scheduleNextEvent();
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
  std::vector<uint32_t> events{Event::FileReadyType::Read, Event::FileReadyType::Write, event_rw};
  for (const auto& e : events) {
    SCOPED_TRACE(absl::StrCat("current event:", e));
    user_file_event_ = Event::UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
        *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, Event::FileTriggerType::Edge,
        event_rw, *io_callback_);
    scheduleNextEvent();
    EXPECT_CALL(ready_cb_, called(event_rw));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    user_file_event_.reset();
  }
}

TEST_F(UserSpaceFileEventImplTest, TestActivateWillSchedule) {
  user_file_event_ = Event::UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, Event::FileTriggerType::Edge,
      event_rw, *io_callback_);
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activate(Event::FileReadyType::Read);
    EXPECT_CALL(ready_cb_, called(event_rw)).Times(1);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activate(Event::FileReadyType::Write);
    EXPECT_CALL(ready_cb_, called(event_rw)).Times(1);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(UserSpaceFileEventImplTest, TestActivateDedup) {
  user_file_event_ = Event::UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, Event::FileTriggerType::Edge,
      event_rw, *io_callback_);
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
  user_file_event_ = Event::UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, Event::FileTriggerType::Edge,
      event_rw, *io_callback_);
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
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
  user_file_event_ = Event::UserSpaceFileEventFactory::createUserSpaceFileEventImpl(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, Event::FileTriggerType::Edge,
      Event::FileReadyType::Write | Event::FileReadyType::Closed, *io_callback_);
  {
    scheduleNextEvent();
    // No Closed event bit if enabled by not activated.
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Write)).Times(1);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activate(Event::FileReadyType::Closed);
    // Activate could deliver Closed event bit.
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Write | Event::FileReadyType::Closed))
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