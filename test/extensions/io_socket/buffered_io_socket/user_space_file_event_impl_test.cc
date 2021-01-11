#include <cstdint>

#include "envoy/event/file_event.h"

#include "common/event/dispatcher_impl.h"

#include "extensions/io_socket/buffered_io_socket/peer_buffer.h"
#include "extensions/io_socket/buffered_io_socket/user_space_file_event_impl.h"

#include "test/mocks/common.h"
#include "test/test_common/environment.h"
#include "test/test_common/test_runtime.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace BufferedIoSocket {
namespace {

using testing::NiceMock;
using testing::Return;

constexpr auto event_rw = Event::FileReadyType::Read | Event::FileReadyType::Write;
class MockReadyCb {
public:
  MOCK_METHOD(void, called, (uint32_t));
};

class MockUserspaceIoHandle : public UserspaceIoHandle {
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

class UserSpaceFileEventImplTest : public testing::Test {
public:
  UserSpaceFileEventImplTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test_thread")) {}

  void setWritable() { EXPECT_CALL(io_source_, isPeerWritable()).WillRepeatedly(Return(true)); }
  void setReadable() { EXPECT_CALL(io_source_, isReadable()).WillRepeatedly(Return(true)); }
  void setWriteEnd() {
    EXPECT_CALL(io_source_, isPeerShutDownWrite()).WillRepeatedly(Return(true));
  }
  void clearEventExpectation() { testing::Mock::VerifyAndClearExpectations(&io_source_); }

protected:
  NiceMock<MockUserspaceIoHandle> io_source_;
  MockReadyCb ready_cb_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::unique_ptr<UserSpaceFileEventImpl> user_file_event_;
};

TEST_F(UserSpaceFileEventImplTest, EnabledEventsTriggeredAfterCreate) {
  for (const auto current_event : {Event::FileReadyType::Read, Event::FileReadyType::Write,
                                   Event::FileReadyType::Read | Event::FileReadyType::Write}) {
    SCOPED_TRACE(absl::StrCat("current event:", current_event));
    clearEventExpectation();
    if (current_event & Event::FileReadyType::Read) {
      setReadable();
    }
    if (current_event & Event::FileReadyType::Write) {
      setWritable();
    }
    MockReadyCb ready_cb;
    auto user_file_event = std::make_unique<UserSpaceFileEventImpl>(
        *dispatcher_, [&ready_cb](uint32_t arg) { ready_cb.called(arg); }, current_event,
        io_source_);
    EXPECT_CALL(ready_cb, called(current_event));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    testing::Mock::VerifyAndClearExpectations(&ready_cb);
  }
}

TEST_F(UserSpaceFileEventImplTest, ReadEventNotDeliveredAfterDisabledRead) {
  setWritable();
  setReadable();
  user_file_event_ = std::make_unique<UserSpaceFileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
  // The above should deliver both Read and Write during the poll. It is not tested here but in
  // other test case.

  // Now disable Read.
  user_file_event_->setEnabled(Event::FileReadyType::Write);
  EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Write));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(UserSpaceFileEventImplTest, RescheduleAfterTriggered) {
  user_file_event_ = std::make_unique<UserSpaceFileEventImpl>(
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
}

TEST_F(UserSpaceFileEventImplTest, RescheduleIsDeduplicated) {
  user_file_event_ = std::make_unique<UserSpaceFileEventImpl>(
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

TEST_F(UserSpaceFileEventImplTest, DefaultReturnAllEnabledReadAndWriteEvents) {
  for (const auto current_event : {Event::FileReadyType::Read, Event::FileReadyType::Write,
                                   Event::FileReadyType::Read | Event::FileReadyType::Write}) {
    SCOPED_TRACE(absl::StrCat("current event:", current_event));
    EXPECT_CALL(io_source_, isReadable())
        .WillOnce(Return((current_event & Event::FileReadyType::Read) != 0))
        .RetiresOnSaturation();
    EXPECT_CALL(io_source_, isPeerWritable())
        .WillOnce(Return((current_event & Event::FileReadyType::Write) != 0))
        .RetiresOnSaturation();
    auto user_file_event = std::make_unique<UserSpaceFileEventImpl>(
        *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); }, event_rw, io_source_);
    EXPECT_CALL(ready_cb_, called(current_event));
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
}

TEST_F(UserSpaceFileEventImplTest, ActivateWillSchedule) {
  // IO is neither readable nor writable.
  user_file_event_ = std::make_unique<UserSpaceFileEventImpl>(
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

TEST_F(UserSpaceFileEventImplTest, ActivateDedup) {
  // IO is neither readable nor writable.
  user_file_event_ = std::make_unique<UserSpaceFileEventImpl>(
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

TEST_F(UserSpaceFileEventImplTest, EnabledClearActivate) {
  // IO is neither readable nor writable.
  user_file_event_ = std::make_unique<UserSpaceFileEventImpl>(
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

TEST_F(UserSpaceFileEventImplTest, EventClosedIsTriggeredBySetWriteEnd) {
  setWriteEnd();
  user_file_event_ = std::make_unique<UserSpaceFileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); },
      Event::FileReadyType::Write | Event::FileReadyType::Closed, io_source_);

  EXPECT_CALL(ready_cb_, called(Event::FileReadyType::Closed));
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
}

TEST_F(UserSpaceFileEventImplTest, EventClosedIsTriggeredByManullyActivate) {
  user_file_event_ = std::make_unique<UserSpaceFileEventImpl>(
      *dispatcher_, [this](uint32_t arg) { ready_cb_.called(arg); },
      Event::FileReadyType::Write | Event::FileReadyType::Closed, io_source_);
  {
    // No Closed event bit if enabled by not activated.
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
} // namespace
} // namespace BufferedIoSocket
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy