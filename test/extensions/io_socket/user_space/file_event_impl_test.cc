#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/event/dispatcher_impl.h"

#include "extensions/io_socket/user_space/file_event_impl.h"
#include "extensions/io_socket/user_space/io_handle.h"

#include "test/mocks/common.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {
namespace {

using testing::NiceMock;
using testing::Return;

constexpr auto event_rw = Event::FileReadyType::Read | Event::FileReadyType::Write;
constexpr auto event_all =
    Event::FileReadyType::Read | Event::FileReadyType::Write | Event::FileReadyType::Closed;
constexpr uint32_t events_all_combination[] = {
    Event::FileReadyType::Read,
    Event::FileReadyType::Write,
    Event::FileReadyType::Closed,
    Event::FileReadyType::Read | Event::FileReadyType::Write,
    Event::FileReadyType::Read | Event::FileReadyType::Closed,
    Event::FileReadyType::Write | Event::FileReadyType::Closed,
    Event::FileReadyType::Read | Event::FileReadyType::Write | Event::FileReadyType::Closed};

class MockReadyCb {
public:
  MOCK_METHOD(void, called, (uint32_t));
};

class MockIoHandle : public IoHandle {
public:
  MOCK_METHOD(void, setWriteEnd, ());
  MOCK_METHOD(bool, isPeerShutDownWrite, (), (const));
  MOCK_METHOD(void, onPeerDestroy, ());
  MOCK_METHOD(void, setNewDataAvailable, ());
  MOCK_METHOD(Buffer::Instance*, getWriteBuffer, ());
  MOCK_METHOD(bool, isWritable, (), (const));
  MOCK_METHOD(bool, isPeerWritable, (), (const));
  MOCK_METHOD(void, onPeerBufferLowWatermark, ());
  MOCK_METHOD(bool, isReadable, (), (const));
};

class FileEventImplTest : public testing::Test {
public:
  FileEventImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void setWritable() { EXPECT_CALL(io_source_, isPeerWritable()).WillRepeatedly(Return(true)); }
  void setReadable() { EXPECT_CALL(io_source_, isReadable()).WillRepeatedly(Return(true)); }
  void setWriteEnd() {
    EXPECT_CALL(io_source_, isPeerShutDownWrite()).WillRepeatedly(Return(true));
  }
  void clearEventExpectation() { testing::Mock::VerifyAndClearExpectations(&io_source_); }

protected:
  NiceMock<MockIoHandle> io_source_;
  MockReadyCb ready_cb_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::unique_ptr<FileEventImpl> user_file_event_;
};

TEST_F(FileEventImplTest, EnabledEventsTriggeredAfterCreate) {
  for (const auto current_event : events_all_combination) {
    SCOPED_TRACE(absl::StrCat("current event:", current_event));
    clearEventExpectation();
    if (current_event & Event::FileReadyType::Read) {
      setReadable();
    }
    if (current_event & Event::FileReadyType::Write) {
      setWritable();
    }
    if (current_event & Event::FileReadyType::Closed) {
      setWriteEnd();
    }
    MockReadyCb ready_cb;
    auto user_file_event = std::make_unique<FileEventImpl>(
        *dispatcher_, [&ready_cb](uint32_t arg) { ready_cb.called(arg); }, current_event,
        io_source_);
    EXPECT_CALL(ready_cb, called(current_event));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    testing::Mock::VerifyAndClearExpectations(&ready_cb);
  }
}

TEST_F(FileEventImplTest, ReadEventIsTriggeredWhenThePeerSetWriteEnd) {
  for (const auto current_event :
       {Event::FileReadyType::Read, Event::FileReadyType::Read | Event::FileReadyType::Closed}) {
    SCOPED_TRACE(absl::StrCat("current event:", current_event));
    clearEventExpectation();
    setWriteEnd();
    MockReadyCb ready_cb;
    auto user_file_event = std::make_unique<FileEventImpl>(
        *dispatcher_, [&ready_cb](uint32_t arg) { ready_cb.called(arg); }, current_event,
        io_source_);
    EXPECT_CALL(ready_cb, called(current_event));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    testing::Mock::VerifyAndClearExpectations(&ready_cb);
  }
}

TEST_F(FileEventImplTest, ReadEventNotDeliveredAfterDisabledRead) {
  setWritable();
  setReadable();
  user_file_event_ = std::make_unique<FileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
  // The above should deliver both Read and Write during the activateIfEnabled(). It is not tested
  // here but in other test case.

  // Now disable Read.
  user_file_event_->setEnabled(Event::FileReadyType::Write);
  EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Write));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(FileEventImplTest, RescheduleAfterTriggered) {
  user_file_event_ = std::make_unique<FileEventImpl>(
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
  {
    SCOPED_TRACE("merge events");
    user_file_event_->activate(Event::FileReadyType::Read);
    user_file_event_->activate(Event::FileReadyType::Write);
    EXPECT_CALL(ready_cb_, called(event_rw));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    SCOPED_TRACE("no auto reschedule");
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(FileEventImplTest, RescheduleIsDeduplicated) {
  user_file_event_ = std::make_unique<FileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
  {
    SCOPED_TRACE("1st schedule");
    user_file_event_->activate(event_rw);

    user_file_event_->activate(event_rw);
    EXPECT_CALL(ready_cb_, called(event_rw));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    SCOPED_TRACE("further dispatcher drive");
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(FileEventImplTest, DefaultReturnAllEnabledReadAndWriteEvents) {
  for (const auto current_event : {Event::FileReadyType::Read, Event::FileReadyType::Write,
                                   Event::FileReadyType::Read | Event::FileReadyType::Write}) {
    SCOPED_TRACE(absl::StrCat("current event:", current_event));
    EXPECT_CALL(io_source_, isReadable())
        .WillOnce(Return((current_event & Event::FileReadyType::Read) != 0))
        .RetiresOnSaturation();
    EXPECT_CALL(io_source_, isPeerWritable())
        .WillOnce(Return((current_event & Event::FileReadyType::Write) != 0))
        .RetiresOnSaturation();
    auto user_file_event = std::make_unique<FileEventImpl>(
        *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
    EXPECT_CALL(ready_cb_, called(current_event));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(FileEventImplTest, ActivateDoesNotHonerEnabled) {
  for (const auto enabled : events_all_combination) {
    auto user_file_event = std::make_unique<FileEventImpl>(
        *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, enabled, io_source_);
    {
      EXPECT_CALL(ready_cb_, called(_)).Times(0);
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    for (const auto active : events_all_combination) {
      user_file_event->activate(active);
      EXPECT_CALL(ready_cb_, called(active));
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }
}

TEST_F(FileEventImplTest, ActivateWillSchedule) {
  // IO is neither readable nor writable.
  user_file_event_ = std::make_unique<FileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activate(Event::FileReadyType::Read);
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Read));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activate(Event::FileReadyType::Write);
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Write));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(FileEventImplTest, ActivateIfEnabledWillSchedule) {
  // IO is neither readable nor writable.
  user_file_event_ = std::make_unique<FileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_all, io_source_);
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  for (const auto current_event : events_all_combination) {
    SCOPED_TRACE(absl::StrCat("current event:", current_event));
    user_file_event_->activateIfEnabled(current_event);
    EXPECT_CALL(ready_cb_, called(current_event));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(FileEventImplTest, ActivateDedup) {
  // IO is neither readable nor writable.
  user_file_event_ = std::make_unique<FileEventImpl>(
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
    EXPECT_CALL(ready_cb_, called(event_rw));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(FileEventImplTest, ActivateIfEnabledCanDedup) {
  // IO is neither readable nor writable.
  user_file_event_ = std::make_unique<FileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_all, io_source_);
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activateIfEnabled(Event::FileReadyType::Read);
    user_file_event_->activateIfEnabled(Event::FileReadyType::Write);
    user_file_event_->activateIfEnabled(Event::FileReadyType::Write);
    user_file_event_->activateIfEnabled(Event::FileReadyType::Read);
    EXPECT_CALL(ready_cb_, called(event_rw));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activateIfEnabled(Event::FileReadyType::Read);
    user_file_event_->activateIfEnabled(Event::FileReadyType::Write);
    user_file_event_->activateIfEnabled(Event::FileReadyType::Write);
    user_file_event_->activateIfEnabled(Event::FileReadyType::Closed);
    EXPECT_CALL(ready_cb_, called(event_all));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(FileEventImplTest, EnabledClearActivate) {
  // IO is neither readable nor writable.
  user_file_event_ = std::make_unique<FileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Ensure both events are pending so that any enabled event will be immediately delivered.
  setWritable();
  setReadable();
  setWriteEnd();
  // The enabled event are delivered but not the other.
  {
    user_file_event_->activate(Event::FileReadyType::Read);
    user_file_event_->setEnabled(Event::FileReadyType::Write);
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Write));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activate(Event::FileReadyType::Write);
    user_file_event_->setEnabled(Event::FileReadyType::Read);
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Read));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // No event is delivered since it's either user disabled or io_handle doesn't provide pending
  // data.
  {
    clearEventExpectation();
    setReadable();
    user_file_event_->activate(Event::FileReadyType::Read);
    user_file_event_->setEnabled(Event::FileReadyType::Write);
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    clearEventExpectation();
    setWritable();
    user_file_event_->activate(Event::FileReadyType::Write);
    user_file_event_->setEnabled(Event::FileReadyType::Read);
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(FileEventImplTest, ActivateIfEnabledTriggerOnlyEnabled) {
  user_file_event_ = std::make_unique<FileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); },
      Event::FileReadyType::Read | Event::FileReadyType::Closed, io_source_);
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // All 3 ready types are queried. However, only enabled events are triggered.
  {
    user_file_event_->activateIfEnabled(Event::FileReadyType::Read | Event::FileReadyType::Write |
                                        Event::FileReadyType::Closed);
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Read | Event::FileReadyType::Closed));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }

  // Below events contains Read but not Closed. The callback sees Read.
  {
    user_file_event_->activateIfEnabled(Event::FileReadyType::Read);
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Read));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activateIfEnabled(Event::FileReadyType::Read | Event::FileReadyType::Write);
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Read));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  // Below ready types has no overlap with enabled. No callback is triggered.
  {
    user_file_event_->activateIfEnabled(Event::FileReadyType::Write);
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activateIfEnabled(0);
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(FileEventImplTest, EventClosedIsTriggeredBySetWriteEnd) {
  setWriteEnd();
  user_file_event_ = std::make_unique<FileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); },
      Event::FileReadyType::Write | Event::FileReadyType::Closed, io_source_);

  EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Closed));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(FileEventImplTest, EventClosedIsTriggeredByManullyActivate) {
  user_file_event_ = std::make_unique<FileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); },
      Event::FileReadyType::Write | Event::FileReadyType::Closed, io_source_);
  {
    // No Closed event bit if enabled but not activated.
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    user_file_event_->activate(Event::FileReadyType::Closed);
    // Activate could deliver Closed event bit.
    EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Closed));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  {
    EXPECT_CALL(ready_cb_, called(_)).Times(0);
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(FileEventImplTest, NotImplementedEmulatedEdge) {
  user_file_event_ = std::make_unique<FileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); },
      Event::FileReadyType::Write | Event::FileReadyType::Closed, io_source_);
  EXPECT_DEATH({ user_file_event_->registerEventIfEmulatedEdge(0); }, "not implemented");
  EXPECT_DEATH({ user_file_event_->unregisterEventIfEmulatedEdge(0); }, "not implemented");
}
} // namespace
} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy